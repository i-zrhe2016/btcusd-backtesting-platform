use std::{
    env,
    fs::{self, File},
    path::PathBuf,
    process::Command,
    sync::Arc,
};

use anyhow::{bail, Context};
use arrow_array::{ArrayRef, Float64Array, Int64Array, RecordBatch};
use arrow_schema::{DataType, Field, Schema};
use chrono::{SecondsFormat, TimeZone, Utc};
use parquet::{
    arrow::ArrowWriter,
    basic::Compression,
    file::properties::WriterProperties,
};
use serde_json::json;

#[derive(Debug)]
struct Candle {
    timestamp: i64,
    open: f64,
    high: f64,
    low: f64,
    close: f64,
    volume: f64,
}

fn main() -> anyhow::Result<()> {
    let output_path = env::args()
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("data/market/btcusd_1m.parquet"));
    let hours = env::args()
        .nth(2)
        .map(|value| value.parse::<i64>())
        .transpose()
        .context("hours must be an integer")?
        .unwrap_or(4)
        .clamp(1, 5);

    let end = Utc::now().timestamp().div_euclid(60) * 60;
    let start = end - hours * 60 * 60;
    let candles = fetch_coinbase_candles(start, end)?;
    if candles.is_empty() {
        bail!("Coinbase returned no BTC-USD candles");
    }

    if let Some(parent) = output_path.parent() {
        fs::create_dir_all(parent)
            .with_context(|| format!("failed to create {}", parent.display()))?;
    }
    let temporary_path = output_path.with_extension("parquet.tmp");
    write_parquet(&temporary_path, &candles)?;
    fs::rename(&temporary_path, &output_path)
        .with_context(|| format!("failed to move fixture to {}", output_path.display()))?;
    write_manifest(&output_path, &candles, start, end)?;

    println!(
        "wrote {} candles to {}",
        candles.len(),
        output_path.display()
    );
    Ok(())
}

fn fetch_coinbase_candles(start: i64, end: i64) -> anyhow::Result<Vec<Candle>> {
    let url = format!(
        "https://api.exchange.coinbase.com/products/BTC-USD/candles?granularity=60&start={}&end={}",
        iso8601(start)?,
        iso8601(end)?,
    );
    let output = Command::new("curl")
        .args([
            "--fail",
            "--silent",
            "--show-error",
            "--connect-timeout",
            "10",
            "--max-time",
            "30",
            "-H",
            "Accept: application/json",
            "-H",
            "User-Agent: btcusd-backtesting-platform-fixture/0.1",
            &url,
        ])
        .output()
        .context("failed to run curl")?;
    if !output.status.success() {
        bail!(
            "Coinbase request failed: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        );
    }

    let rows: Vec<Vec<f64>> =
        serde_json::from_slice(&output.stdout).context("failed to parse Coinbase response")?;
    let mut candles = Vec::new();
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
        if is_valid(&candle) {
            candles.push(candle);
        }
    }
    candles.sort_unstable_by_key(|candle| candle.timestamp);
    candles.dedup_by_key(|candle| candle.timestamp);
    Ok(candles)
}

fn write_parquet(path: &PathBuf, candles: &[Candle]) -> anyhow::Result<()> {
    let schema = Arc::new(Schema::new(vec![
        Field::new("timestamp", DataType::Int64, false),
        Field::new("open", DataType::Float64, false),
        Field::new("high", DataType::Float64, false),
        Field::new("low", DataType::Float64, false),
        Field::new("close", DataType::Float64, false),
        Field::new("volume", DataType::Float64, false),
    ]));

    let batch = RecordBatch::try_new(
        schema.clone(),
        vec![
            Arc::new(Int64Array::from(
                candles.iter().map(|candle| candle.timestamp).collect::<Vec<_>>(),
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
                candles.iter().map(|candle| candle.close).collect::<Vec<_>>(),
            )),
            Arc::new(Float64Array::from(
                candles.iter().map(|candle| candle.volume).collect::<Vec<_>>(),
            )),
        ],
    )?;
    let file = File::create(path).with_context(|| format!("failed to create {}", path.display()))?;
    let properties = WriterProperties::builder()
        .set_compression(Compression::SNAPPY)
        .build();
    let mut writer = ArrowWriter::try_new(file, schema, Some(properties))?;
    writer.write(&batch)?;
    writer.close()?;
    Ok(())
}

fn write_manifest(path: &PathBuf, candles: &[Candle], start: i64, end: i64) -> anyhow::Result<()> {
    let sha256 = sha256sum(path)?;
    let manifest = json!({
        "source": "Coinbase Exchange BTC-USD candles",
        "purpose": "local docker compose smoke fixture",
        "generated_at_utc": iso8601(Utc::now().timestamp())?,
        "request_start_utc": iso8601(start)?,
        "request_end_utc": iso8601(end)?,
        "first_timestamp": candles.first().map(|candle| candle.timestamp),
        "last_timestamp": candles.last().map(|candle| candle.timestamp),
        "candle_count": candles.len(),
        "sha256": sha256,
    });
    let manifest_path = path.with_file_name("manifest.json");
    fs::write(&manifest_path, serde_json::to_vec_pretty(&manifest)?)
        .with_context(|| format!("failed to write {}", manifest_path.display()))?;
    Ok(())
}

fn sha256sum(path: &PathBuf) -> anyhow::Result<String> {
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

fn iso8601(timestamp: i64) -> anyhow::Result<String> {
    Ok(Utc
        .timestamp_opt(timestamp, 0)
        .single()
        .context("invalid timestamp")?
        .to_rfc3339_opts(SecondsFormat::Secs, true))
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
