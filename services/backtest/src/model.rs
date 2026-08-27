use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use uuid::Uuid;

pub const GUEST_USER_ID: Uuid = Uuid::from_u128(1);

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StrategyParameters {
    pub length: usize,
    pub oversold: f64,
    pub overbought: f64,
}

impl Default for StrategyParameters {
    fn default() -> Self {
        Self {
            length: 30,
            oversold: 20.0,
            overbought: 80.0,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BacktestRequest {
    #[serde(default = "default_name")]
    pub name: String,
    #[serde(default = "default_symbol")]
    pub symbol: String,
    #[serde(default = "default_timeframe")]
    pub timeframe: String,
    pub from: i64,
    pub to: i64,
    #[serde(default = "default_capital")]
    pub initial_capital: f64,
    #[serde(default = "default_fee_rate")]
    pub fee_rate: f64,
    #[serde(default)]
    pub strategy: StrategyParameters,
}

fn default_name() -> String {
    "Stoch 交叉回测".to_owned()
}
fn default_symbol() -> String {
    "BTCUSD".to_owned()
}
fn default_timeframe() -> String {
    "1h".to_owned()
}
fn default_capital() -> f64 {
    10_000.0
}
fn default_fee_rate() -> f64 {
    0.001
}

impl BacktestRequest {
    pub fn validate(&self) -> Result<(), String> {
        if self.name.trim().is_empty() || self.name.chars().count() > 120 {
            return Err("name must contain between 1 and 120 characters".to_owned());
        }
        if !self.symbol.eq_ignore_ascii_case("BTCUSD")
            && !self.symbol.eq_ignore_ascii_case("BTC-USD")
        {
            return Err("only BTCUSD is supported".to_owned());
        }
        if !["1m", "15m", "30m", "1h", "2h", "4h", "1d", "1w", "1M"]
            .contains(&self.timeframe.as_str())
        {
            return Err("timeframe is not supported".to_owned());
        }
        if self.from >= self.to {
            return Err("from must be earlier than to".to_owned());
        }
        if !self.initial_capital.is_finite() || self.initial_capital <= 0.0 {
            return Err("initial_capital must be positive".to_owned());
        }
        if !self.fee_rate.is_finite() || !(0.0..=0.05).contains(&self.fee_rate) {
            return Err("fee_rate must be between 0 and 0.05".to_owned());
        }
        if !(2..=5_000).contains(&self.strategy.length) {
            return Err("strategy length must be between 2 and 5000".to_owned());
        }
        if !(0.0..100.0).contains(&self.strategy.oversold)
            || !(0.0..=100.0).contains(&self.strategy.overbought)
            || self.strategy.oversold >= self.strategy.overbought
        {
            return Err("strategy thresholds must satisfy 0 <= oversold < overbought <= 100".to_owned());
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Deserialize)]
pub struct Candle {
    pub timestamp: i64,
    pub open: f64,
    pub high: f64,
    pub low: f64,
    pub close: f64,
    pub volume: f64,
}

#[derive(Debug, Clone, Deserialize)]
pub struct StochasticPoint {
    pub timestamp: i64,
    pub k: [Option<f64>; 3],
    pub d: [Option<f64>; 3],
}

#[derive(Debug, Deserialize)]
pub struct MarketSnapshot {
    pub candles: Vec<Candle>,
    pub stochastic: Vec<StochasticPoint>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TradeSide {
    Buy,
    Sell,
}

#[derive(Debug, Clone, Serialize)]
pub struct Trade {
    pub timestamp: i64,
    pub side: TradeSide,
    pub price: f64,
    pub quantity: f64,
    pub fee: f64,
    pub realized_pnl: Option<f64>,
}

#[derive(Debug, Clone, Serialize)]
pub struct BacktestResult {
    pub initial_capital: f64,
    pub final_equity: f64,
    pub total_return_pct: f64,
    pub max_drawdown_pct: f64,
    pub completed_trades: usize,
    pub win_rate_pct: f64,
    pub buy_and_hold_return_pct: f64,
    pub candles_processed: usize,
    pub trades: Vec<Trade>,
}

#[derive(Debug, Clone, Serialize, sqlx::FromRow)]
pub struct BacktestRecord {
    pub id: Uuid,
    pub user_id: Uuid,
    pub name: String,
    pub status: String,
    pub config: Value,
    pub result: Option<Value>,
    pub error_message: Option<String>,
    pub created_at: DateTime<Utc>,
    pub completed_at: Option<DateTime<Utc>>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct StrategyConfigRequest {
    pub name: String,
    pub parameters: StrategyParameters,
}

#[derive(Debug, Clone, Serialize, sqlx::FromRow)]
pub struct StrategyConfigRecord {
    pub id: Uuid,
    pub user_id: Uuid,
    pub name: String,
    pub parameters: Value,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}
