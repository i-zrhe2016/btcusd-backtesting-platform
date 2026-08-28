export interface Candle {
  timestamp: number
  open: number
  high: number
  low: number
  close: number
  volume: number
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
}
