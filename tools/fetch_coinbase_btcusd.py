#!/usr/bin/env python3

from __future__ import annotations

import datetime as dt
import concurrent.futures
import json
import shutil
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path


API_URL = "https://api.exchange.coinbase.com/products/BTC-USD/candles"
OUTPUT_PATH = Path(__file__).resolve().parents[1] / "src" / "embedded_btcusd_data.h"
START_UTC = dt.datetime(2016, 1, 1, 0, 0, 0, tzinfo=dt.timezone.utc)
END_UTC = dt.datetime(2019, 1, 1, 0, 0, 0, tzinfo=dt.timezone.utc)
GRANULARITY_SECONDS = 60
MAX_CANDLES_PER_REQUEST = 300
REQUEST_PAUSE_SECONDS = 0.08
MAX_RETRIES = 5
FETCH_WORKERS = 6


def isoformat_z(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def fetch_chunk(start_utc: dt.datetime, end_utc: dt.datetime) -> list[list[float]]:
    params = urllib.parse.urlencode(
        {
            "granularity": str(GRANULARITY_SECONDS),
            "start": isoformat_z(start_utc),
            "end": isoformat_z(end_utc),
        }
    )
    url = f"{API_URL}?{params}"

    last_error: Exception | None = None
    for attempt in range(MAX_RETRIES):
        try:
            # curl is used here because it reuses the system TLS stack more
            # reliably than urllib in the Windows/Linux build environments.
            result = subprocess.run(
                [
                    "curl", "--fail", "--silent", "--show-error",
                    "--connect-timeout", "10", "--max-time", "30",
                    "-H", "Accept: application/json",
                    "-H", "User-Agent: BtcUsdReplayBuilder/1.0",
                    url,
                ],
                check=False,
                capture_output=True,
                timeout=40,
            )
            if result.returncode != 0:
                raise RuntimeError(result.stderr.decode("utf-8", errors="replace").strip())
            payload = result.stdout.decode("utf-8")
            data = json.loads(payload)
            if not isinstance(data, list):
                raise RuntimeError(f"Unexpected API response: {payload[:200]}")
            return data
        except (subprocess.TimeoutExpired, TimeoutError, json.JSONDecodeError, RuntimeError) as exc:
            last_error = exc
            time.sleep((attempt + 1) * 0.6)

    raise RuntimeError(f"Failed to fetch {start_utc} - {end_utc}: {last_error}")


def format_number(value: float) -> str:
    return format(value, ".15g")


def build_header_prefix(
    generated_at_utc: dt.datetime,
    start_utc: dt.datetime,
    last_timestamp: int,
    candle_count: int,
    missing_count: int,
) -> str:
    coverage_end_text = dt.datetime.fromtimestamp(
        last_timestamp, tz=dt.timezone.utc
    ).strftime("%Y-%m-%d %H:%M:%S UTC")
    return f"""#pragma once

namespace embedded_btcusd_data {{

inline constexpr const char kExchange[] = "Coinbase Exchange";
inline constexpr const char kProductId[] = "BTC-USD";
inline constexpr const char kGranularity[] = "1m";
inline constexpr const char kGeneratedAtUtc[] = "{generated_at_utc.strftime("%Y-%m-%d %H:%M:%S UTC")}";
inline constexpr const char kCoverageStartUtc[] = "{start_utc.strftime("%Y-%m-%d %H:%M:%S UTC")}";
inline constexpr const char kCoverageEndUtc[] = "{coverage_end_text}";
inline constexpr long long kCoverageStartTimestamp = {int(start_utc.timestamp())};
inline constexpr long long kCoverageEndTimestamp = {last_timestamp};
inline constexpr int kCandleCount = {candle_count};
inline constexpr int kMissingMinuteBuckets = {missing_count};
inline constexpr const char kCsv[] = R"BTCUSDCSV("""


def build_header_suffix() -> str:
    return """)BTCUSDCSV";

}  // namespace embedded_btcusd_data
"""


def normalize_chunk_rows(
    chunk_rows: list[list[float]],
    start_timestamp: int,
    end_timestamp: int,
) -> list[tuple[int, float, float, float, float]]:
    rows: list[tuple[int, float, float, float, float]] = []
    for row in chunk_rows:
        if not isinstance(row, list) or len(row) < 5:
            continue
        timestamp = int(row[0])
        if timestamp < start_timestamp or timestamp >= end_timestamp:
            continue
        rows.append((timestamp, float(row[3]), float(row[2]), float(row[1]), float(row[4])))
    rows.sort(key=lambda row: row[0])
    return rows


def format_row(row: tuple[int, float, float, float, float]) -> str:
    timestamp, open_, high, low, close = row
    return ",".join(
        [str(timestamp), format_number(open_), format_number(high), format_number(low), format_number(close)]
    ) + "\n"


def write_embedded_header(
    raw_path: Path,
    output_path: Path,
    generated_at_utc: dt.datetime,
    start_utc: dt.datetime,
    last_timestamp: int,
    candle_count: int,
    expected_count: int,
) -> None:
    temporary_path = output_path.with_suffix(".h.tmp")
    with temporary_path.open("w", encoding="utf-8", newline="") as output:
        output.write(build_header_prefix(
            generated_at_utc,
            start_utc,
            last_timestamp,
            candle_count,
            expected_count - candle_count,
        ))
        with raw_path.open("r", encoding="utf-8", newline="") as raw:
            shutil.copyfileobj(raw, output, length=1024 * 1024)
        output.write(build_header_suffix())
    temporary_path.replace(output_path)


def main() -> int:
    now_utc = dt.datetime.now(dt.timezone.utc)
    start_utc = START_UTC
    end_exclusive_utc = END_UTC
    if end_exclusive_utc <= start_utc:
        raise RuntimeError("Current time is earlier than the requested start date.")

    cursor = start_utc
    chunks: list[tuple[dt.datetime, dt.datetime]] = []
    while cursor < end_exclusive_utc:
        chunk_end = min(
            cursor + dt.timedelta(seconds=GRANULARITY_SECONDS * MAX_CANDLES_PER_REQUEST),
            end_exclusive_utc,
        )
        chunks.append((cursor, chunk_end))
        cursor = chunk_end

    start_timestamp = int(start_utc.timestamp())
    end_timestamp = int(end_exclusive_utc.timestamp())
    raw_path = OUTPUT_PATH.with_suffix(".csv.tmp")
    candle_count = 0
    first_timestamp = 0
    last_timestamp = 0
    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=FETCH_WORKERS) as executor:
            # Keep futures in chronological order. Each completed chunk is
            # normalized and written immediately, so the full multi-year
            # dataset never has to coexist in memory.
            futures = [executor.submit(fetch_chunk, start, end) for start, end in chunks]
            with raw_path.open("w", encoding="utf-8", newline="") as raw:
                for completed, future in enumerate(futures, start=1):
                    chunk_rows = future.result()
                    # Release each completed response immediately; retaining
                    # all Future results would recreate the memory problem.
                    futures[completed - 1] = None
                    rows = normalize_chunk_rows(
                        chunk_rows, start_timestamp, end_timestamp
                    )
                    del chunk_rows
                    for row in rows:
                        if row[0] <= last_timestamp:
                            continue
                        raw.write(format_row(row))
                        if candle_count == 0:
                            first_timestamp = row[0]
                        last_timestamp = row[0]
                        candle_count += 1
                    if completed % 100 == 0 or completed == len(chunks):
                        print(f"Fetched {completed}/{len(chunks)} chunks", flush=True)
                    if REQUEST_PAUSE_SECONDS > 0:
                        time.sleep(REQUEST_PAUSE_SECONDS)

        if candle_count == 0:
            raise RuntimeError("No BTC-USD candles were fetched.")

        expected_count = int((end_exclusive_utc - start_utc).total_seconds() // GRANULARITY_SECONDS)
        OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
        write_embedded_header(
            raw_path,
            OUTPUT_PATH,
            now_utc,
            start_utc,
            last_timestamp,
            candle_count,
            expected_count,
        )
    finally:
        raw_path.unlink(missing_ok=True)

    total_requests = len(chunks)
    expected_count = int((end_exclusive_utc - start_utc).total_seconds() // GRANULARITY_SECONDS)
    print(f"Output: {OUTPUT_PATH}")
    print(f"Requests: {total_requests}")
    print(f"Candles: {candle_count}")
    print(f"Expected minute buckets: {expected_count}")
    print(
        "Coverage: "
        f"{dt.datetime.fromtimestamp(first_timestamp, tz=dt.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')} -> "
        f"{dt.datetime.fromtimestamp(last_timestamp, tz=dt.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')}"
    )
    print(f"Missing minute buckets: {expected_count - candle_count}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
