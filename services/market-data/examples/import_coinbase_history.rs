use std::{
    collections::BTreeMap,
    env,
    fs::{self, File, OpenOptions},
    io::{BufRead, BufReader, BufWriter, Seek, SeekFrom, Write},
    path::{Path, PathBuf},
    process::Command,
    sync::{mpsc, Arc, Mutex},
    thread,
    time::{Duration, Instant},
};

use anyhow::{bail, Context};
use arrow_array::{ArrayRef, Float64Array, Int64Array, RecordBatch};
use arrow_schema::{DataType, Field, Schema};
use chrono::{DateTime, SecondsFormat, TimeZone, Utc};
use parquet::{arrow::ArrowWriter, basic::Compression, file::properties::WriterProperties};
use serde::{Deserialize, Serialize};
use serde_json::json;

const API_URL: &str = "https://api.exchange.coinbase.com/products/BTC-USD/candles";
const GRANULARITY_SECONDS: i64 = 60;
const MAX_CANDLES_PER_REQUEST: i64 = 300;
const DEFAULT_WORKERS: usize = 6;
const DEFAULT_PAUSE_MS: u64 = 120;
const MAX_RETRIES: usize = 5;
const CSV_HEADER: &str = "timestamp,open,high,low,close,volume\n";

#[derive(Debug, Clone, Copy)]
struct Candle {
    timestamp: i64,
    open: f64,
    high: f64,
    low: f64,
    close: f64,
    volume: f64,
}

#[derive(Debug, Clone, Copy)]
struct Job {
    index: usize,
    start: i64,
    end: i64,
}

#[derive(Debug)]
struct ChunkResult {
    index: usize,
    candles: anyhow::Result<Vec<Candle>>,
}

#[derive(Debug, Serialize, Deserialize)]
struct Progress {
    version: u32,
    start_timestamp: i64,
    end_timestamp: i64,
    total_chunks: usize,
    completed_chunks: usize,
    raw_bytes: u64,
    candle_count: usize,
    first_timestamp: Option<i64>,
    last_timestamp: Option<i64>,
}

fn main() -> anyhow::Result<()> {
    let mut args = env::args().skip(1);
    let output_path = args
        .next()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("data/market/btcusd_1m.parquet"));
    let start_text = args
        .next()
        .unwrap_or_else(|| "2016-01-01T00:00:00Z".to_owned());
    let end_text = args.next().unwrap_or_else(|| "now".to_owned());
    if args.next().is_some() {
        bail!("usage: import_coinbase_history [output] [start-rfc3339] [end-rfc3339|now]");
    }

    let workers = env_usize("COINBASE_FETCH_WORKERS", DEFAULT_WORKERS).clamp(1, 12);
    let pause = Duration::from_millis(env_u64("COINBASE_REQUEST_PAUSE_MS", DEFAULT_PAUSE_MS));
    let raw_path = sibling_path(&output_path, ".history.csv.part");
    let progress_path = sibling_path(&output_path, ".history.progress.json");
    let parquet_temp_path = sibling_path(&output_path, ".history.parquet.tmp");
    let manifest_path = output_path
        .parent()
        .unwrap_or_else(|| Path::new("."))
        .join("manifest.json");

    let requested_start = parse_timestamp(&start_text).context("invalid start timestamp")?;
    let requested_end = parse_end_timestamp(&end_text).context("invalid end timestamp")?;
    validate_range(requested_start, requested_end)?;

    let existing_progress = read_progress(&progress_path)?;
    let (start, end) = if let Some(progress) = existing_progress.as_ref() {
        if start_text != "2016-01-01T00:00:00Z" && requested_start != progress.start_timestamp {
            bail!(
                "resume checkpoint uses start {}, but requested start is {}; remove {} to start a new import",
                iso8601(progress.start_timestamp)?,
                iso8601(requested_start)?,
                progress_path.display()
            );
        }
        if end_text == "now" {
            (progress.start_timestamp, progress.end_timestamp)
        } else {
            validate_range(requested_start, requested_end)?;
            (requested_start, requested_end)
        }
    } else {
        (requested_start, requested_end)
    };
    validate_minute_boundary(start, "start")?;
    validate_minute_boundary(end, "end")?;

    let chunks = build_chunks(start, end);
    if chunks.is_empty() {
        bail!("the requested range contains no complete minutes");
    }
    let total_chunks = chunks.len();
    let mut progress = prepare_progress(existing_progress, &raw_path, start, end, total_chunks)?;

    if let Some(parent) = output_path.parent() {
        fs::create_dir_all(parent)
            .with_context(|| format!("failed to create {}", parent.display()))?;
    }

    println!(
        "Importing Coinbase BTC-USD 1m history: {} -> {} ({} chunks, {} workers)",
        iso8601(start)?,
        iso8601(end)?,
        total_chunks,
        workers
    );
    if progress.completed_chunks > 0 {
        println!(
            "Resuming at chunk {}/{} from {} candles",
            progress.completed_chunks, total_chunks, progress.candle_count
        );
    }

    fetch_chunks(
        &raw_path,
        &progress_path,
        &chunks,
        &mut progress,
        workers,
        pause,
    )?;

    println!("Converting ordered CSV to Parquet...");
    convert_csv_to_parquet(&raw_path, &parquet_temp_path, &progress)?;

    if output_path.exists() {
        let previous_path = sibling_path(&output_path, ".previous");
        fs::copy(&output_path, &previous_path).with_context(|| {
            format!(
                "failed to preserve the previous Parquet file at {}",
                previous_path.display()
            )
        })?;
        println!("Preserved previous Parquet at {}", previous_path.display());
    }
    fs::rename(&parquet_temp_path, &output_path).with_context(|| {
        format!(
            "failed to atomically replace Parquet at {}",
            output_path.display()
        )
    })?;
    write_manifest(
        &manifest_path,
        &output_path,
        start,
        end,
        progress.first_timestamp,
        progress.last_timestamp,
        progress.candle_count,
    )?;

    fs::remove_file(&raw_path).ok();
    fs::remove_file(&progress_path).ok();
    println!(
        "Imported {} candles to {} ({} -> {}, {} missing minute buckets)",
        progress.candle_count,
        output_path.display(),
        progress
            .first_timestamp
            .map(iso8601)
            .transpose()?
            .unwrap_or_else(|| "none".to_owned()),
        progress
            .last_timestamp
            .map(iso8601)
            .transpose()?
            .unwrap_or_else(|| "none".to_owned()),
        expected_minutes(start, end).saturating_sub(progress.candle_count as i64)
    );
    Ok(())
}

fn fetch_chunks(
    raw_path: &Path,
    progress_path: &Path,
    chunks: &[(i64, i64)],
    progress: &mut Progress,
    workers: usize,
    pause: Duration,
) -> anyhow::Result<()> {
    let raw_file = OpenOptions::new()
        .create(true)
        .read(true)
        .write(true)
        .open(raw_path)
        .with_context(|| format!("failed to open {}", raw_path.display()))?;
    raw_file
        .set_len(progress.raw_bytes)
        .with_context(|| format!("failed to truncate {}", raw_path.display()))?;
    let mut raw = BufWriter::new(raw_file);
    raw.seek(SeekFrom::Start(progress.raw_bytes))?;
    if progress.completed_chunks == 0 && progress.raw_bytes == 0 {
        raw.write_all(CSV_HEADER.as_bytes())?;
        raw.flush()?;
        progress.raw_bytes = CSV_HEADER.len() as u64;
        write_progress(progress_path, progress)?;
    }

    let (job_tx, job_rx) = mpsc::channel::<Job>();
    let (result_tx, result_rx) = mpsc::channel::<ChunkResult>();
    let receiver = Arc::new(Mutex::new(job_rx));
    let limiter = Arc::new(Mutex::new(Instant::now()));
    let mut handles = Vec::with_capacity(workers);
    for _ in 0..workers {
        let receiver = Arc::clone(&receiver);
        let result_tx = result_tx.clone();
        let limiter = Arc::clone(&limiter);
        handles.push(thread::spawn(move || loop {
            let job = match receiver.lock().expect("job receiver mutex poisoned").recv() {
                Ok(job) => job,
                Err(_) => break,
            };
            let candles = fetch_chunk(job.start, job.end, &limiter, pause);
            if result_tx
                .send(ChunkResult {
                    index: job.index,
                    candles,
                })
                .is_err()
            {
                break;
            }
        }));
    }
    drop(result_tx);

    let result = run_scheduler(
        &job_tx,
        &result_rx,
        chunks,
        progress,
        progress_path,
        &mut raw,
        workers,
    );
    drop(job_tx);
    for handle in handles {
        handle
            .join()
            .map_err(|_| anyhow::anyhow!("a Coinbase fetch worker panicked"))?;
    }
    result
}

fn run_scheduler(
    job_tx: &mpsc::Sender<Job>,
    result_rx: &mpsc::Receiver<ChunkResult>,
    chunks: &[(i64, i64)],
    progress: &mut Progress,
    progress_path: &Path,
    raw: &mut BufWriter<File>,
    workers: usize,
) -> anyhow::Result<()> {
    let total = chunks.len();
    let window = workers.saturating_mul(2).max(1);
    let mut scheduled = progress.completed_chunks;
    let mut in_flight = 0usize;
    let mut next_to_write = progress.completed_chunks;
    let mut pending = BTreeMap::<usize, anyhow::Result<Vec<Candle>>>::new();

    while scheduled < total && in_flight < window {
        send_job(job_tx, scheduled, chunks[scheduled])?;
        scheduled += 1;
        in_flight += 1;
    }

    while next_to_write < total {
        let result = result_rx
            .recv()
            .context("Coinbase fetch workers stopped before completing the import")?;
        in_flight = in_flight.saturating_sub(1);
        pending.insert(result.index, result.candles);

        while let Some(candles) = pending.remove(&next_to_write) {
            let candles = candles.with_context(|| {
                format!("failed to fetch chunk {}/{}", next_to_write + 1, total)
            })?;
            append_chunk(raw, progress, progress_path, next_to_write, &candles)?;
            next_to_write += 1;
        }

        while scheduled < total && in_flight < window {
            send_job(job_tx, scheduled, chunks[scheduled])?;
            scheduled += 1;
            in_flight += 1;
        }
    }
    raw.flush()?;
    raw.get_ref().sync_data()?;
    if progress.completed_chunks != total {
        progress.completed_chunks = total;
        progress.raw_bytes = raw.get_ref().metadata()?.len();
        write_progress(progress_path, progress)?;
    }
    Ok(())
}

fn send_job(tx: &mpsc::Sender<Job>, index: usize, (start, end): (i64, i64)) -> anyhow::Result<()> {
    tx.send(Job { index, start, end })
        .context("Coinbase fetch workers stopped")
}

fn append_chunk(
    raw: &mut BufWriter<File>,
    progress: &mut Progress,
    progress_path: &Path,
    index: usize,
    candles: &[Candle],
) -> anyhow::Result<()> {
    if index != progress.completed_chunks {
        bail!(
            "received chunk {} while expecting chunk {}",
            index,
            progress.completed_chunks
        );
    }
    for candle in candles {
        if progress
            .last_timestamp
            .is_some_and(|last| candle.timestamp <= last)
        {
            continue;
        }
        writeln!(
            raw,
            "{},{},{},{},{},{}",
            candle.timestamp, candle.open, candle.high, candle.low, candle.close, candle.volume
        )?;
        progress.first_timestamp.get_or_insert(candle.timestamp);
        progress.last_timestamp = Some(candle.timestamp);
        progress.candle_count += 1;
    }
    raw.flush()?;
    raw.get_ref().sync_data()?;
    progress.completed_chunks += 1;
    progress.raw_bytes = raw.get_ref().metadata()?.len();
    if progress.completed_chunks % 10 == 0 || progress.completed_chunks == progress.total_chunks {
        write_progress(progress_path, progress)?;
    }
    if progress.completed_chunks % 100 == 0 || progress.completed_chunks == progress.total_chunks {
        println!(
            "Fetched {}/{} chunks ({} candles)",
            progress.completed_chunks, progress.total_chunks, progress.candle_count
        );
    }
    Ok(())
}

fn fetch_chunk(
    start: i64,
    end: i64,
    limiter: &Mutex<Instant>,
    pause: Duration,
) -> anyhow::Result<Vec<Candle>> {
    let url = format!(
        "{API_URL}?granularity={GRANULARITY_SECONDS}&start={}&end={}",
        iso8601(start)?,
        iso8601(end)?,
    );
    let mut last_error = String::new();
    for attempt in 0..MAX_RETRIES {
        wait_for_request_slot(limiter, pause);
        let output = Command::new("curl")
            .args([
                "--fail",
                "--silent",
                "--show-error",
                "--connect-timeout",
                "10",
                "--max-time",
                "40",
                "-H",
                "Accept: application/json",
                "-H",
                "User-Agent: btcusd-backtesting-platform-history/0.1",
                &url,
            ])
            .output()
            .context("failed to run curl")?;
        if output.status.success() {
            let rows: Vec<Vec<f64>> = serde_json::from_slice(&output.stdout)
                .with_context(|| format!("failed to parse Coinbase response for {start}"))?;
            let mut candles = Vec::with_capacity(rows.len());
            for row in rows {
                if row.len() < 6 {
                    continue;
                }
                let candle = Candle {
                    timestamp: row[0] as i64,
                    low: row[1],
                    high: row[2],
                    open: row[3],
                    close: row[4],
                    volume: row[5],
                };
                if candle.timestamp >= start && candle.timestamp < end && is_valid(&candle) {
                    candles.push(candle);
                }
            }
            candles.sort_unstable_by_key(|candle| candle.timestamp);
            candles.dedup_by_key(|candle| candle.timestamp);
            return Ok(candles);
        }

        last_error = String::from_utf8_lossy(&output.stderr).trim().to_owned();
        if attempt + 1 < MAX_RETRIES {
            let wait = if last_error.contains("429") {
                Duration::from_secs(10 * 2u64.pow(attempt as u32))
            } else {
                Duration::from_millis(600 * (attempt as u64 + 1))
            };
            thread::sleep(wait);
        }
    }
    bail!("Coinbase request failed for {start}-{end}: {last_error}")
}

fn wait_for_request_slot(limiter: &Mutex<Instant>, pause: Duration) {
    let wait = {
        let mut next = limiter.lock().expect("request limiter mutex poisoned");
        let now = Instant::now();
        let wait = next.saturating_duration_since(now);
        *next = now.max(*next) + pause;
        wait
    };
    if !wait.is_zero() {
        thread::sleep(wait);
    }
}

fn convert_csv_to_parquet(
    raw_path: &Path,
    parquet_path: &Path,
    progress: &Progress,
) -> anyhow::Result<()> {
    let file = File::open(raw_path)
        .with_context(|| format!("failed to open raw history {}", raw_path.display()))?;
    let mut lines = BufReader::new(file).lines();
    let header = lines
        .next()
        .transpose()?
        .context("history CSV is missing its header")?;
    if header != CSV_HEADER.trim_end() {
        bail!("unexpected history CSV header: {header}");
    }

    let schema = Arc::new(Schema::new(vec![
        Field::new("timestamp", DataType::Int64, false),
        Field::new("open", DataType::Float64, false),
        Field::new("high", DataType::Float64, false),
        Field::new("low", DataType::Float64, false),
        Field::new("close", DataType::Float64, false),
        Field::new("volume", DataType::Float64, false),
    ]));
    if let Some(parent) = parquet_path.parent() {
        fs::create_dir_all(parent)?;
    }
    let output = File::create(parquet_path)
        .with_context(|| format!("failed to create {}", parquet_path.display()))?;
    let properties = WriterProperties::builder()
        .set_compression(Compression::SNAPPY)
        .build();
    let mut writer = ArrowWriter::try_new(output, schema.clone(), Some(properties))?;
    let mut batch = Vec::with_capacity(100_000);
    let mut previous_timestamp = None;
    let mut parsed_count = 0usize;
    for line in lines {
        let line = line.context("failed to read history CSV")?;
        let candle = parse_csv_candle(&line)?;
        if previous_timestamp.is_some_and(|previous| candle.timestamp <= previous) {
            bail!(
                "history CSV timestamps are not strictly ascending at {}",
                candle.timestamp
            );
        }
        previous_timestamp = Some(candle.timestamp);
        batch.push(candle);
        parsed_count += 1;
        if batch.len() == 100_000 {
            write_batch(&mut writer, &schema, &batch)?;
            batch.clear();
        }
    }
    if !batch.is_empty() {
        write_batch(&mut writer, &schema, &batch)?;
    }
    writer.close()?;
    if parsed_count != progress.candle_count {
        bail!(
            "history CSV has {} candles, checkpoint says {}",
            parsed_count,
            progress.candle_count
        );
    }
    if parsed_count == 0 {
        bail!("history CSV contains no valid candles");
    }
    Ok(())
}

fn parse_csv_candle(line: &str) -> anyhow::Result<Candle> {
    let fields: Vec<&str> = line.split(',').collect();
    if fields.len() != 6 {
        bail!("history CSV row must have six fields: {line}");
    }
    let candle = Candle {
        timestamp: fields[0].parse().context("invalid CSV timestamp")?,
        open: fields[1].parse().context("invalid CSV open")?,
        high: fields[2].parse().context("invalid CSV high")?,
        low: fields[3].parse().context("invalid CSV low")?,
        close: fields[4].parse().context("invalid CSV close")?,
        volume: fields[5].parse().context("invalid CSV volume")?,
    };
    if !is_valid(&candle) || candle.timestamp % GRANULARITY_SECONDS != 0 {
        bail!("invalid OHLCV candle at {}", candle.timestamp);
    }
    Ok(candle)
}

fn write_batch(
    writer: &mut ArrowWriter<File>,
    schema: &Arc<Schema>,
    candles: &[Candle],
) -> anyhow::Result<()> {
    let batch = RecordBatch::try_new(
        schema.clone(),
        vec![
            Arc::new(Int64Array::from(
                candles
                    .iter()
                    .map(|candle| candle.timestamp)
                    .collect::<Vec<_>>(),
            )) as ArrayRef,
            Arc::new(Float64Array::from(
                candles.iter().map(|candle| candle.open).collect::<Vec<_>>(),
            )),
            Arc::new(Float64Array::from(
                candles.iter().map(|candle| candle.high).collect::<Vec<_>>(),
            )),
            Arc::new(Float64Array::from(
                candles.iter().map(|candle| candle.low).collect::<Vec<_>>(),
            )),
            Arc::new(Float64Array::from(
                candles
                    .iter()
                    .map(|candle| candle.close)
                    .collect::<Vec<_>>(),
            )),
            Arc::new(Float64Array::from(
                candles
                    .iter()
                    .map(|candle| candle.volume)
                    .collect::<Vec<_>>(),
            )),
        ],
    )?;
    writer.write(&batch)?;
    Ok(())
}

fn write_manifest(
    manifest_path: &Path,
    output_path: &Path,
    start: i64,
    end: i64,
    first_timestamp: Option<i64>,
    last_timestamp: Option<i64>,
    candle_count: usize,
) -> anyhow::Result<()> {
    let sha256 = sha256sum(output_path)?;
    let generated_at = Utc::now().timestamp();
    let manifest = json!({
        "source": "Coinbase Exchange BTC-USD candles",
        "product_id": "BTC-USD",
        "granularity": "1m",
        "purpose": "historical BTC-USD OHLCV dataset",
        "generated_at_utc": iso8601(generated_at)?,
        "request_start_utc": iso8601(start)?,
        "request_end_utc": iso8601(end)?,
        "first_timestamp": first_timestamp,
        "last_timestamp": last_timestamp,
        "candle_count": candle_count,
        "expected_minute_buckets": expected_minutes(start, end),
        "missing_minute_buckets": expected_minutes(start, end).saturating_sub(candle_count as i64),
        "sha256": sha256,
    });
    let temporary_path = sibling_path(manifest_path, ".tmp");
    fs::write(&temporary_path, serde_json::to_vec_pretty(&manifest)?)
        .with_context(|| format!("failed to write {}", temporary_path.display()))?;
    fs::rename(&temporary_path, manifest_path)
        .with_context(|| format!("failed to replace {}", manifest_path.display()))?;
    Ok(())
}

fn prepare_progress(
    existing: Option<Progress>,
    raw_path: &Path,
    start: i64,
    end: i64,
    total_chunks: usize,
) -> anyhow::Result<Progress> {
    let Some(mut progress) = existing else {
        return Ok(Progress {
            version: 1,
            start_timestamp: start,
            end_timestamp: end,
            total_chunks,
            completed_chunks: 0,
            raw_bytes: 0,
            candle_count: 0,
            first_timestamp: None,
            last_timestamp: None,
        });
    };
    if progress.version != 1
        || progress.start_timestamp != start
        || progress.end_timestamp != end
        || progress.total_chunks != total_chunks
    {
        bail!(
            "history checkpoint does not match the requested range; remove {} to start over",
            sibling_path(raw_path, ".progress.json").display()
        );
    }
    if progress.completed_chunks > total_chunks {
        bail!("history checkpoint has too many completed chunks");
    }
    if !raw_path.exists() && progress.raw_bytes > 0 {
        bail!(
            "history checkpoint exists but {} is missing",
            raw_path.display()
        );
    }
    if raw_path.exists() {
        let size = fs::metadata(raw_path)?.len();
        if size < progress.raw_bytes {
            bail!(
                "history CSV is shorter than its checkpoint ({} < {})",
                size,
                progress.raw_bytes
            );
        }
    }
    progress.raw_bytes = progress.raw_bytes.max(CSV_HEADER.len() as u64);
    Ok(progress)
}

fn read_progress(path: &Path) -> anyhow::Result<Option<Progress>> {
    if !path.exists() {
        return Ok(None);
    }
    let bytes = fs::read(path).with_context(|| format!("failed to read {}", path.display()))?;
    let progress = serde_json::from_slice(&bytes)
        .with_context(|| format!("failed to parse {}", path.display()))?;
    Ok(Some(progress))
}

fn write_progress(path: &Path, progress: &Progress) -> anyhow::Result<()> {
    let temporary_path = sibling_path(path, ".tmp");
    fs::write(&temporary_path, serde_json::to_vec_pretty(progress)?)
        .with_context(|| format!("failed to write {}", temporary_path.display()))?;
    fs::rename(&temporary_path, path)
        .with_context(|| format!("failed to replace {}", path.display()))?;
    Ok(())
}

fn build_chunks(start: i64, end: i64) -> Vec<(i64, i64)> {
    let mut chunks = Vec::new();
    let mut cursor = start;
    while cursor < end {
        let chunk_end = (cursor + GRANULARITY_SECONDS * MAX_CANDLES_PER_REQUEST).min(end);
        chunks.push((cursor, chunk_end));
        cursor = chunk_end;
    }
    chunks
}

fn parse_timestamp(value: &str) -> anyhow::Result<i64> {
    let datetime = DateTime::parse_from_rfc3339(value)
        .with_context(|| format!("{value} must be RFC3339, for example 2016-01-01T00:00:00Z"))?;
    Ok(datetime.timestamp())
}

fn parse_end_timestamp(value: &str) -> anyhow::Result<i64> {
    if value.eq_ignore_ascii_case("now") {
        return Ok(Utc::now().timestamp().div_euclid(GRANULARITY_SECONDS) * GRANULARITY_SECONDS);
    }
    parse_timestamp(value)
}

fn validate_range(start: i64, end: i64) -> anyhow::Result<()> {
    if start >= end {
        bail!("start must be earlier than end");
    }
    Ok(())
}

fn validate_minute_boundary(timestamp: i64, name: &str) -> anyhow::Result<()> {
    if timestamp % GRANULARITY_SECONDS != 0 {
        bail!("{name} timestamp must align to a whole minute");
    }
    Ok(())
}

fn expected_minutes(start: i64, end: i64) -> i64 {
    (end - start).div_euclid(GRANULARITY_SECONDS)
}

fn sibling_path(path: &Path, suffix: &str) -> PathBuf {
    PathBuf::from(format!("{}{}", path.display(), suffix))
}

fn iso8601(timestamp: i64) -> anyhow::Result<String> {
    Ok(Utc
        .timestamp_opt(timestamp, 0)
        .single()
        .context("invalid Unix timestamp")?
        .to_rfc3339_opts(SecondsFormat::Secs, true))
}

fn sha256sum(path: &Path) -> anyhow::Result<String> {
    let output = Command::new("sha256sum")
        .arg(path)
        .output()
        .context("failed to run sha256sum")?;
    if !output.status.success() {
        bail!(
            "sha256sum failed: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        );
    }
    Ok(String::from_utf8_lossy(&output.stdout)
        .split_whitespace()
        .next()
        .unwrap_or_default()
        .to_owned())
}

fn env_usize(name: &str, default: usize) -> usize {
    env::var(name)
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(default)
}

fn env_u64(name: &str, default: u64) -> u64 {
    env::var(name)
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(default)
}

fn is_valid(candle: &Candle) -> bool {
    candle.open.is_finite()
        && candle.high.is_finite()
        && candle.low.is_finite()
        && candle.close.is_finite()
        && candle.volume.is_finite()
        && candle.open > 0.0
        && candle.close > 0.0
        && candle.low > 0.0
        && candle.high >= candle.open.max(candle.close).max(candle.low)
        && candle.low <= candle.open.min(candle.close).min(candle.high)
        && candle.volume >= 0.0
}
