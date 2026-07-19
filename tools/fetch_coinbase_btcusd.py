#!/usr/bin/env python3

from __future__ import annotations

import datetime as dt
import concurrent.futures
import json
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path


API_URL = "https://api.exchange.coinbase.com/products/BTC-USD/candles"
OUTPUT_PATH = Path(__file__).resolve().parents[1] / "src" / "embedded_btcusd_data.h"
LOOKBACK_DAYS = 365
GRANULARITY_SECONDS = 60
MAX_CANDLES_PER_REQUEST = 300
REQUEST_PAUSE_SECONDS = 0.08
MAX_RETRIES = 5
FETCH_WORKERS = 6


def isoformat_z(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def latest_complete_minute_end(now_utc: dt.datetime) -> dt.datetime:
    rounded = now_utc.replace(second=0, microsecond=0)
    return rounded


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


def build_header(
    rows: list[tuple[int, float, float, float, float]],
    generated_at_utc: dt.datetime,
    start_utc: dt.datetime,
    coverage_end_utc: dt.datetime,
) -> str:
    csv_lines = ["timestamp,open,high,low,close"]
    for timestamp, open_, high, low, close in rows:
        csv_lines.append(
            ",".join(
                [
                    str(timestamp),
                    format_number(open_),
                    format_number(high),
                    format_number(low),
                    format_number(close),
                ]
            )
        )

    csv_text = "\n".join(csv_lines) + "\n"
    expected_count = int((coverage_end_utc - start_utc).total_seconds() // GRANULARITY_SECONDS)
    missing_count = expected_count - len(rows)
    last_timestamp = rows[-1][0] if rows else 0

    return f"""#pragma once

namespace embedded_btcusd_data {{

inline constexpr const char kExchange[] = "Coinbase Exchange";
inline constexpr const char kProductId[] = "BTC-USD";
inline constexpr const char kGranularity[] = "1m";
inline constexpr const char kGeneratedAtUtc[] = "{generated_at_utc.strftime("%Y-%m-%d %H:%M:%S UTC")}";
inline constexpr const char kCoverageStartUtc[] = "{start_utc.strftime("%Y-%m-%d %H:%M:%S UTC")}";
inline constexpr const char kCoverageEndUtc[] = "{dt.datetime.fromtimestamp(last_timestamp, tz=dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")}";
inline constexpr long long kCoverageStartTimestamp = {int(start_utc.timestamp())};
inline constexpr long long kCoverageEndTimestamp = {last_timestamp};
inline constexpr int kCandleCount = {len(rows)};
inline constexpr int kMissingMinuteBuckets = {missing_count};
inline constexpr const char kCsv[] = R"BTCUSDCSV({csv_text})BTCUSDCSV";

}}  // namespace embedded_btcusd_data
"""


def main() -> int:
    now_utc = dt.datetime.now(dt.timezone.utc)
    end_exclusive_utc = latest_complete_minute_end(now_utc)
    start_utc = end_exclusive_utc - dt.timedelta(days=LOOKBACK_DAYS)
    if end_exclusive_utc <= start_utc:
        raise RuntimeError("Current time is earlier than the requested start date.")

    all_rows: dict[int, tuple[float, float, float, float]] = {}
    cursor = start_utc
    chunks: list[tuple[dt.datetime, dt.datetime]] = []
    while cursor < end_exclusive_utc:
        chunk_end = min(
            cursor + dt.timedelta(seconds=GRANULARITY_SECONDS * MAX_CANDLES_PER_REQUEST),
            end_exclusive_utc,
        )
        chunks.append((cursor, chunk_end))
        cursor = chunk_end

    with concurrent.futures.ThreadPoolExecutor(max_workers=FETCH_WORKERS) as executor:
        futures = {executor.submit(fetch_chunk, start, end): (start, end) for start, end in chunks}
        for completed, future in enumerate(concurrent.futures.as_completed(futures), start=1):
            chunk_rows = future.result()
            for row in chunk_rows:
                if not isinstance(row, list) or len(row) < 5:
                    continue

                timestamp = int(row[0])
                low = float(row[1])
                high = float(row[2])
                open_ = float(row[3])
                close = float(row[4])

                if timestamp < int(start_utc.timestamp()) or timestamp >= int(end_exclusive_utc.timestamp()):
                    continue

                all_rows[timestamp] = (open_, high, low, close)
            if completed % 100 == 0 or completed == len(chunks):
                print(f"Fetched {completed}/{len(chunks)} chunks", flush=True)
            if REQUEST_PAUSE_SECONDS > 0:
                time.sleep(REQUEST_PAUSE_SECONDS)

    total_requests = len(chunks)

    rows = [
        (timestamp, values[0], values[1], values[2], values[3])
        for timestamp, values in sorted(all_rows.items())
    ]
    if not rows:
        raise RuntimeError("No BTC-USD candles were fetched.")

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text(build_header(rows, now_utc, start_utc, end_exclusive_utc), encoding="utf-8")

    expected_count = int((end_exclusive_utc - start_utc).total_seconds() // GRANULARITY_SECONDS)
    print(f"Output: {OUTPUT_PATH}")
    print(f"Requests: {total_requests}")
    print(f"Candles: {len(rows)}")
    print(f"Expected minute buckets: {expected_count}")
    print(
        "Coverage: "
        f"{dt.datetime.fromtimestamp(rows[0][0], tz=dt.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')} -> "
        f"{dt.datetime.fromtimestamp(rows[-1][0], tz=dt.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')}"
    )
    print(f"Missing minute buckets: {expected_count - len(rows)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
