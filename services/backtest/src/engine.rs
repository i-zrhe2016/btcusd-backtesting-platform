use crate::{
    error::ApiError,
    model::{BacktestRequest, BacktestResult, MarketSnapshot, Trade, TradeSide},
};

pub fn run(request: &BacktestRequest, market: &MarketSnapshot) -> Result<BacktestResult, ApiError> {
    if market.candles.len() < 2 || market.candles.len() != market.stochastic.len() {
        return Err(ApiError::MarketData(
            "not enough aligned candles and indicators for a backtest".to_owned(),
        ));
    }

    let mut cash = request.initial_capital;
    let mut quantity = 0.0;
    let mut entry_cost = 0.0;
    let mut trades = Vec::new();
    let mut wins = 0usize;
    let mut completed_trades = 0usize;
    let mut peak_equity = request.initial_capital;
    let mut max_drawdown = 0.0f64;

    for index in 1..market.candles.len() {
        let candle = &market.candles[index];
        let previous = &market.stochastic[index - 1];
        let current = &market.stochastic[index];
        let (Some(previous_k), Some(previous_d), Some(k), Some(d)) =
            (previous.k[0], previous.d[0], current.k[0], current.d[0])
        else {
            continue;
        };

        let crosses_up = previous_k <= previous_d && k > d;
        let crosses_down = previous_k >= previous_d && k < d;
        if quantity == 0.0 && crosses_up && k <= request.strategy.oversold {
            let notional = cash / (1.0 + request.fee_rate);
            let fee = notional * request.fee_rate;
            quantity = notional / candle.close;
            entry_cost = cash;
            cash = 0.0;
            trades.push(Trade {
                timestamp: candle.timestamp,
                side: TradeSide::Buy,
                price: candle.close,
                quantity,
                fee,
                realized_pnl: None,
            });
        } else if quantity > 0.0 && crosses_down && k >= request.strategy.overbought {
            let gross = quantity * candle.close;
            let fee = gross * request.fee_rate;
            cash = gross - fee;
            let pnl = cash - entry_cost;
            if pnl > 0.0 {
                wins += 1;
            }
            completed_trades += 1;
            trades.push(Trade {
                timestamp: candle.timestamp,
                side: TradeSide::Sell,
                price: candle.close,
                quantity,
                fee,
                realized_pnl: Some(pnl),
            });
            quantity = 0.0;
            entry_cost = 0.0;
        }

        let equity = cash + quantity * candle.close;
        peak_equity = peak_equity.max(equity);
        if peak_equity > 0.0 {
            max_drawdown = max_drawdown.max((peak_equity - equity) / peak_equity * 100.0);
        }
    }

    let first_close = market.candles.first().unwrap().close;
    let last_close = market.candles.last().unwrap().close;
    let final_equity = cash + quantity * last_close;
    Ok(BacktestResult {
        initial_capital: request.initial_capital,
        final_equity,
        total_return_pct: (final_equity / request.initial_capital - 1.0) * 100.0,
        max_drawdown_pct: max_drawdown,
        completed_trades,
        win_rate_pct: if completed_trades == 0 {
            0.0
        } else {
            wins as f64 / completed_trades as f64 * 100.0
        },
        buy_and_hold_return_pct: (last_close / first_close - 1.0) * 100.0,
        candles_processed: market.candles.len(),
        trades,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{Candle, StochasticPoint, StrategyParameters};

    fn candle(timestamp: i64, close: f64) -> Candle {
        Candle {
            timestamp,
            open: close,
            high: close,
            low: close,
            close,
            volume: 1.0,
        }
    }

    fn point(timestamp: i64, k: f64, d: f64) -> StochasticPoint {
        StochasticPoint {
            timestamp,
            k: [Some(k), None, None],
            d: [Some(d), None, None],
        }
    }

    #[test]
    fn buys_low_cross_and_sells_high_cross() {
        let request = BacktestRequest {
            name: "test".into(),
            symbol: "BTCUSD".into(),
            timeframe: "1h".into(),
            from: 0,
            to: 240,
            initial_capital: 1_000.0,
            fee_rate: 0.0,
            strategy: StrategyParameters {
                length: 3,
                oversold: 30.0,
                overbought: 70.0,
            },
        };
        let market = MarketSnapshot {
            candles: vec![candle(0, 10.0), candle(60, 10.0), candle(120, 12.0), candle(180, 15.0)],
            stochastic: vec![point(0, 20.0, 21.0), point(60, 22.0, 21.0), point(120, 80.0, 79.0), point(180, 78.0, 79.0)],
        };
        let result = run(&request, &market).unwrap();
        assert_eq!(result.completed_trades, 1);
        assert_eq!(result.trades.len(), 2);
        assert!((result.final_equity - 1_500.0).abs() < 0.001);
        assert_eq!(result.win_rate_pct, 100.0);
    }
}

