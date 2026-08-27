export interface Candle {
  timestamp: number
  open: number
  high: number
  low: number
  close: number
  volume: number
}

export interface StochasticPoint {
  timestamp: number
  k: [number | null, number | null, number | null]
  d: [number | null, number | null, number | null]
}

export interface MarketMetadata {
  symbol: string
  base_timeframe: string
  source: string
  candle_count: number
  first_timestamp: number
  last_timestamp: number
  supported_timeframes: string[]
}

export interface MarketSnapshot {
  symbol: string
  timeframe: string
  source: string
  candles: Candle[]
  stochastic: StochasticPoint[]
}

export interface BacktestRequest {
  name: string
  symbol: string
  timeframe: string
  from: number
  to: number
  initial_capital: number
  fee_rate: number
  strategy: {
    length: number
    oversold: number
    overbought: number
  }
}

export interface Trade {
  timestamp: number
  side: 'buy' | 'sell'
  price: number
  quantity: number
  fee: number
  realized_pnl: number | null
}

export interface BacktestResult {
  initial_capital: number
  final_equity: number
  total_return_pct: number
  max_drawdown_pct: number
  completed_trades: number
  win_rate_pct: number
  buy_and_hold_return_pct: number
  candles_processed: number
  trades: Trade[]
}

export interface BacktestRecord {
  id: string
  user_id: string
  name: string
  status: 'running' | 'completed' | 'failed'
  config: BacktestRequest
  result: BacktestResult | null
  error_message: string | null
  created_at: string
  completed_at: string | null
}

