use chrono::{Datelike, TimeZone, Utc};

use crate::{
    error::ApiError,
    indicator::calculate_stochastic,
    model::{Candle, MarketSnapshot, SnapshotQuery, Timeframe},
};

const MAX_CANDLES: usize = 5_000;

pub fn snapshot(all_candles: &[Candle], query: SnapshotQuery) -> Result<MarketSnapshot, ApiError> {
    if !query.symbol.eq_ignore_ascii_case("BTCUSD")
        && !query.symbol.eq_ignore_ascii_case("BTC-USD")
    {
        return Err(ApiError::BadRequest(
            "only BTCUSD is available in this deployment".to_owned(),
        ));
    }
    let timeframe = Timeframe::parse(&query.timeframe).ok_or_else(|| {
        ApiError::BadRequest(format!("unsupported timeframe {}", query.timeframe))
    })?;
    if query.limit == 0 || query.limit > MAX_CANDLES {
        return Err(ApiError::BadRequest(format!(
            "limit must be between 1 and {MAX_CANDLES}"
        )));
    }
    if query.from.zip(query.to).is_some_and(|(from, to)| from >= to) {
        return Err(ApiError::BadRequest(
            "from must be earlier than to".to_owned(),
        ));
    }
    let lengths = parse_lengths(query.lengths.as_deref())?;

    let filtered: Vec<Candle> = all_candles
        .iter()
        .copied()
        .filter(|candle| query.from.is_none_or(|from| candle.timestamp >= from))
        .filter(|candle| query.to.is_none_or(|to| candle.timestamp < to))
        .collect();
    let mut candles = aggregate(&filtered, timeframe);
    if candles.len() > query.limit {
        candles.drain(..candles.len() - query.limit);
    }
    let stochastic = calculate_stochastic(&candles, lengths);

    Ok(MarketSnapshot {
        symbol: "BTCUSD".to_owned(),
        timeframe: timeframe.as_str().to_owned(),
        source: "Parquet",
        candles,
        stochastic,
    })
}

fn parse_lengths(value: Option<&str>) -> Result<[usize; 3], ApiError> {
    let Some(value) = value else {
        return Ok([30, 120, 840]);
    };
    let parsed: Vec<usize> = value
        .split(',')
        .map(str::trim)
        .map(|part| part.parse::<usize>())
        .collect::<Result<_, _>>()
        .map_err(|_| ApiError::BadRequest("lengths must contain three integers".to_owned()))?;
    if parsed.len() != 3 || parsed.iter().any(|length| !(2..=5_000).contains(length)) {
        return Err(ApiError::BadRequest(
            "lengths must contain three integers between 2 and 5000".to_owned(),
        ));
    }
    Ok([parsed[0], parsed[1], parsed[2]])
}

pub fn aggregate(candles: &[Candle], timeframe: Timeframe) -> Vec<Candle> {
    let mut result: Vec<Candle> = Vec::new();
    for candle in candles {
        let bucket = bucket_start(candle.timestamp, timeframe);
        if let Some(current) = result.last_mut().filter(|current| current.timestamp == bucket) {
            current.high = current.high.max(candle.high);
            current.low = current.low.min(candle.low);
            current.close = candle.close;
            current.volume += candle.volume;
        } else {
            result.push(Candle {
                timestamp: bucket,
                ..*candle
            });
        }
    }
    result
}

fn bucket_start(timestamp: i64, timeframe: Timeframe) -> i64 {
    let seconds = match timeframe {
        Timeframe::Minute1 => Some(60),
        Timeframe::Minute15 => Some(15 * 60),
        Timeframe::Minute30 => Some(30 * 60),
        Timeframe::Hour1 => Some(60 * 60),
        Timeframe::Hour2 => Some(2 * 60 * 60),
        Timeframe::Hour4 => Some(4 * 60 * 60),
        Timeframe::Day1 => Some(24 * 60 * 60),
        Timeframe::Week1 | Timeframe::Month1 => None,
    };
    if let Some(seconds) = seconds {
        return timestamp.div_euclid(seconds) * seconds;
    }
    if timeframe == Timeframe::Week1 {
        const MONDAY_OFFSET: i64 = 3 * 24 * 60 * 60;
        const WEEK: i64 = 7 * 24 * 60 * 60;
        return (timestamp + MONDAY_OFFSET).div_euclid(WEEK) * WEEK - MONDAY_OFFSET;
    }

    let datetime = Utc
        .timestamp_opt(timestamp, 0)
        .single()
        .expect("valid Unix timestamp");
    Utc.with_ymd_and_hms(datetime.year(), datetime.month(), 1, 0, 0, 0)
        .single()
        .expect("valid month start")
        .timestamp()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn candle(timestamp: i64, value: f64) -> Candle {
        Candle {
            timestamp,
            open: value,
            high: value + 2.0,
            low: value - 1.0,
            close: value + 1.0,
            volume: 3.0,
        }
    }

    #[test]
    fn aggregates_ohlcv_by_timeframe() {
        let result = aggregate(
            &[candle(0, 10.0), candle(60, 12.0), candle(900, 20.0)],
            Timeframe::Minute15,
        );
        assert_eq!(result.len(), 2);
        assert_eq!(result[0].open, 10.0);
        assert_eq!(result[0].high, 14.0);
        assert_eq!(result[0].low, 9.0);
        assert_eq!(result[0].close, 13.0);
        assert_eq!(result[0].volume, 6.0);
    }

    #[test]
    fn monday_is_week_bucket_start() {
        assert_eq!(bucket_start(345_600, Timeframe::Week1), 345_600);
    }
}

