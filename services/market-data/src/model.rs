use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, Serialize)]
pub struct Candle {
    pub timestamp: i64,
    pub open: f64,
    pub high: f64,
    pub low: f64,
    pub close: f64,
    pub volume: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Timeframe {
    Minute1,
    Minute15,
    Minute30,
    Hour1,
    Hour2,
    Hour4,
    Day1,
    Week1,
    Month1,
}

impl Timeframe {
    pub fn parse(value: &str) -> Option<Self> {
        match value {
            "1m" => Some(Self::Minute1),
            "15m" => Some(Self::Minute15),
            "30m" => Some(Self::Minute30),
            "1h" => Some(Self::Hour1),
            "2h" => Some(Self::Hour2),
            "4h" => Some(Self::Hour4),
            "1d" => Some(Self::Day1),
            "1w" => Some(Self::Week1),
            "1M" => Some(Self::Month1),
            _ => None,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Minute1 => "1m",
            Self::Minute15 => "15m",
            Self::Minute30 => "30m",
            Self::Hour1 => "1h",
            Self::Hour2 => "2h",
            Self::Hour4 => "4h",
            Self::Day1 => "1d",
            Self::Week1 => "1w",
            Self::Month1 => "1M",
        }
    }
}

#[derive(Debug, Deserialize)]
pub struct SnapshotQuery {
    #[serde(default = "default_symbol")]
    pub symbol: String,
    #[serde(default = "default_timeframe")]
    pub timeframe: String,
    pub from: Option<i64>,
    pub to: Option<i64>,
    #[serde(default = "default_limit")]
    pub limit: usize,
    pub lengths: Option<String>,
}

fn default_symbol() -> String {
    "BTCUSD".to_owned()
}

fn default_timeframe() -> String {
    "1h".to_owned()
}

fn default_limit() -> usize {
    500
}

#[derive(Debug, Serialize)]
pub struct MarketMetadata {
    pub symbol: &'static str,
    pub base_timeframe: &'static str,
    pub source: &'static str,
    pub candle_count: usize,
    pub first_timestamp: i64,
    pub last_timestamp: i64,
    pub supported_timeframes: [&'static str; 9],
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StochasticPoint {
    pub timestamp: i64,
    pub k: [Option<f64>; 3],
    pub d: [Option<f64>; 3],
}

#[derive(Debug, Serialize)]
pub struct MarketSnapshot {
    pub symbol: String,
    pub timeframe: String,
    pub source: &'static str,
    pub candles: Vec<Candle>,
    pub stochastic: Vec<StochasticPoint>,
}

