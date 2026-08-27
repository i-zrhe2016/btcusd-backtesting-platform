use std::collections::VecDeque;

use crate::model::{Candle, StochasticPoint};

pub fn calculate_stochastic(candles: &[Candle], lengths: [usize; 3]) -> Vec<StochasticPoint> {
    let pairs = lengths.map(|length| calculate_pair(candles, length.max(1)));

    candles
        .iter()
        .enumerate()
        .map(|(index, candle)| StochasticPoint {
            timestamp: candle.timestamp,
            k: std::array::from_fn(|group| finite(pairs[group].0[index])),
            d: std::array::from_fn(|group| finite(pairs[group].1[index])),
        })
        .collect()
}

fn calculate_pair(candles: &[Candle], length: usize) -> (Vec<f64>, Vec<f64>) {
    let smooth_length = ((length as f64 / 10.0) * 3.0).round().max(1.0) as usize;
    let mut raw_k = vec![f64::NAN; candles.len()];
    let mut lowest: VecDeque<usize> = VecDeque::new();
    let mut highest: VecDeque<usize> = VecDeque::new();

    for index in 0..candles.len() {
        while lowest
            .back()
            .is_some_and(|candidate| candles[*candidate].close >= candles[index].close)
        {
            lowest.pop_back();
        }
        lowest.push_back(index);
        while highest
            .back()
            .is_some_and(|candidate| candles[*candidate].close <= candles[index].close)
        {
            highest.pop_back();
        }
        highest.push_back(index);

        let first = (index + 1).saturating_sub(length);
        while lowest.front().is_some_and(|candidate| *candidate < first) {
            lowest.pop_front();
        }
        while highest.front().is_some_and(|candidate| *candidate < first) {
            highest.pop_front();
        }
        if index + 1 < length {
            continue;
        }

        let low = candles[*lowest.front().expect("low window is populated")].close;
        let high = candles[*highest.front().expect("high window is populated")].close;
        raw_k[index] = if high == low {
            50.0
        } else {
            (candles[index].close - low) / (high - low) * 100.0
        };
    }

    let k = simple_moving_average(&raw_k, smooth_length);
    let d = simple_moving_average(&k, smooth_length);
    (k, d)
}

fn simple_moving_average(values: &[f64], length: usize) -> Vec<f64> {
    let mut result = vec![f64::NAN; values.len()];
    let mut sum = 0.0;
    let mut valid = 0usize;
    for (index, value) in values.iter().copied().enumerate() {
        if value.is_finite() {
            sum += value;
            valid += 1;
        }
        if index >= length {
            let expired = values[index - length];
            if expired.is_finite() {
                sum -= expired;
                valid -= 1;
            }
        }
        if index + 1 >= length && valid == length {
            result[index] = sum / length as f64;
        }
    }
    result
}

fn finite(value: f64) -> Option<f64> {
    value.is_finite().then_some(value)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn flat_market_uses_fifty_after_warm_up() {
        let candles: Vec<_> = (0..20)
            .map(|index| Candle {
                timestamp: index * 60,
                open: 10.0,
                high: 10.0,
                low: 10.0,
                close: 10.0,
                volume: 1.0,
            })
            .collect();
        let result = calculate_stochastic(&candles, [3, 5, 7]);
        assert_eq!(result.last().unwrap().k[0], Some(50.0));
        assert_eq!(result.last().unwrap().d[0], Some(50.0));
    }
}
