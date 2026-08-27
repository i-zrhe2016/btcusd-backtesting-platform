import type {
  BacktestRecord,
  BacktestRequest,
  MarketMetadata,
  MarketSnapshot,
} from './types'

interface ApiErrorBody {
  code?: string
  message?: string
}

async function requestJson<T>(input: RequestInfo | URL, init?: RequestInit): Promise<T> {
  const response = await fetch(input, {
    ...init,
    headers: {
      Accept: 'application/json',
      ...(init?.body ? { 'Content-Type': 'application/json' } : {}),
      ...init?.headers,
    },
  })
  if (!response.ok) {
    let body: ApiErrorBody = {}
    try {
      body = (await response.json()) as ApiErrorBody
    } catch {
      // The status text is a safe fallback when an upstream returns no JSON.
    }
    throw new Error(body.message || `请求失败（HTTP ${response.status}）`)
  }
  return (await response.json()) as T
}

export function getMarketMetadata(): Promise<MarketMetadata> {
  return requestJson('/api/market/meta')
}

export function getMarketSnapshot(params: {
  timeframe: string
  from: number
  to: number
  limit?: number
  lengths: [number, number, number]
}): Promise<MarketSnapshot> {
  const query = new URLSearchParams({
    symbol: 'BTCUSD',
    timeframe: params.timeframe,
    from: String(params.from),
    to: String(params.to),
    limit: String(params.limit ?? 500),
    lengths: params.lengths.join(','),
  })
  return requestJson(`/api/market/snapshot?${query}`)
}

export function createBacktest(payload: BacktestRequest): Promise<BacktestRecord> {
  return requestJson('/api/backtests', {
    method: 'POST',
    body: JSON.stringify(payload),
  })
}

export function listBacktests(limit = 12): Promise<BacktestRecord[]> {
  return requestJson(`/api/backtests?limit=${limit}`)
}

