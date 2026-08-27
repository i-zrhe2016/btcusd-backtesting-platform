use std::{fs::File, path::Path};

use anyhow::{bail, Context};
use arrow_array::{Array, Float64Array, Int64Array};
use parquet::arrow::arrow_reader::ParquetRecordBatchReaderBuilder;

use crate::model::Candle;

pub fn load_candles(path: &Path) -> anyhow::Result<Vec<Candle>> {
    let file = File::open(path)
        .with_context(|| format!("failed to open Parquet file {}", path.display()))?;
    let builder = ParquetRecordBatchReaderBuilder::try_new(file)
        .context("failed to inspect Parquet schema")?;
    let mut reader = builder
        .with_batch_size(16_384)
        .build()
        .context("failed to create Parquet reader")?;
    let mut candles = Vec::new();

    for batch in &mut reader {
        let batch = batch.context("failed to read a Parquet record batch")?;
        let timestamp = column::<Int64Array>(&batch, "timestamp")?;
        let open = column::<Float64Array>(&batch, "open")?;
        let high = column::<Float64Array>(&batch, "high")?;
        let low = column::<Float64Array>(&batch, "low")?;
        let close = column::<Float64Array>(&batch, "close")?;
        let volume = column::<Float64Array>(&batch, "volume")?;

        for index in 0..batch.num_rows() {
            if timestamp.is_null(index)
                || open.is_null(index)
                || high.is_null(index)
                || low.is_null(index)
                || close.is_null(index)
                || volume.is_null(index)
            {
                continue;
            }
            candles.push(Candle {
                timestamp: timestamp.value(index),
                open: open.value(index),
                high: high.value(index),
                low: low.value(index),
                close: close.value(index),
                volume: volume.value(index),
            });
        }
    }

    candles.sort_unstable_by_key(|candle| candle.timestamp);
    candles.dedup_by_key(|candle| candle.timestamp);
    if candles.is_empty() {
        bail!("Parquet file contains no valid candles");
    }
    Ok(candles)
}

fn column<'a, T: 'static>(
    batch: &'a arrow_array::RecordBatch,
    name: &str,
) -> anyhow::Result<&'a T> {
    let index = batch
        .schema()
        .index_of(name)
        .with_context(|| format!("missing required Parquet column {name}"))?;
    batch
        .column(index)
        .as_any()
        .downcast_ref::<T>()
        .with_context(|| format!("Parquet column {name} has an invalid type"))
}

