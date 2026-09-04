import type { Candle, MarketMetadata } from './types'
import { fromUtcInput, toUtcInput, formatUtc } from './format'

export interface ReplayProjectDraft {
  name: string
  symbol: string
  timeframe: string
  fromInput: string
  toInput: string
  speed: number
}

export type ManualTradeSide = 'buy' | 'sell'
export type ManualTradeAction = 'open-long' | 'close-long' | 'open-short' | 'close-short'
export type ManualPositionSide = 'flat' | 'long' | 'short'

export interface ManualTrade {
  id: string
  candleIndex: number
  timestamp: number
  side: ManualTradeSide
  action: ManualTradeAction
  price: number
  quantity: number
  realizedPnl: number | null
  createdAt: string
}

export interface TrendLinePoint {
  timestamp: number
  price: number
}

export interface TrendLine {
  id: string
  start: TrendLinePoint
  end: TrendLinePoint
  createdAt: string
}

export interface TrendLineInput {
  start: TrendLinePoint
  end: TrendLinePoint
}

export interface ManualTradeInput {
  side: ManualTradeSide
  candleIndex: number
  timestamp: number
  price: number
}

export interface ManualPositionSummary {
  side: ManualPositionSide
  quantity: number
  averagePrice: number | null
  realizedPnl: number
  unrealizedPnl: number
  netPnl: number
  tradeCount: number
  lastTrade: ManualTrade | null
}

export interface ReplayProject {
  id: string
  name: string
  symbol: string
  timeframe: string
  from: number
  to: number
  speed: number
  cursorIndex: number
  cursorTimestamp?: number
  trades: ManualTrade[]
  trendLines: TrendLine[]
  createdAt: string
  updatedAt: string
}

interface StorageLike {
  getItem(key: string): string | null
  setItem(key: string, value: string): void
}

const PROJECTS_KEY = 'btcusd.replay.projects.v1'
const ACTIVE_KEY = 'btcusd.replay.active.v1'

const DEFAULT_SPEED = 1
const MIN_SPEED = 0.25
const MAX_SPEED = 8
export const MANUAL_TRADE_QUANTITY = 1
export const REPLAY_PAGE_SIZE = 240

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max)
}

function nowIso(): string {
  return new Date().toISOString()
}

function createId(): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID()
  }
  return `replay_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`
}

function defaultStorage(): StorageLike | null {
  if (typeof window === 'undefined') return null
  return window.localStorage
}

export function normalizeSpeed(value: number): number {
  return clamp(Number.isFinite(value) ? value : DEFAULT_SPEED, MIN_SPEED, MAX_SPEED)
}

export function replayDelayMs(speed: number): number {
  return Math.max(120, Math.round(900 / normalizeSpeed(speed)))
}

export function clampReplayCursor(index: number, totalCandles: number): number {
  if (!Number.isFinite(index) || totalCandles <= 0) return 0
  return clamp(Math.floor(index), 0, totalCandles - 1)
}

export function replayCursorIndexAtOrBefore(candles: Pick<Candle, 'timestamp'>[], timestamp: number): number {
  if (!candles.length || !Number.isFinite(timestamp)) return 0

  let low = 0
  let high = candles.length - 1
  let result = 0
  while (low <= high) {
    const middle = Math.floor((low + high) / 2)
    if (candles[middle].timestamp <= timestamp) {
      result = middle
      low = middle + 1
    } else {
      high = middle - 1
    }
  }
  return result
}

export function timeframeStepSeconds(timeframe: string): number {
  switch (timeframe) {
    case '15m':
      return 15 * 60
    case '30m':
      return 30 * 60
    case '1h':
      return 60 * 60
    case '2h':
      return 2 * 60 * 60
    case '4h':
      return 4 * 60 * 60
    case '1d':
      return 24 * 60 * 60
    case '1w':
      return 7 * 24 * 60 * 60
    case '1M':
      return 31 * 24 * 60 * 60
    case '1m':
    default:
      return 60
  }
}

export function replayWindowEnd(from: number, to: number, timeframe: string): number {
  return Math.min(to, from + (timeframeStepSeconds(timeframe) * REPLAY_PAGE_SIZE))
}

export function nextReplayWindowStart(
  lastTimestamp: number | undefined,
  timeframe: string,
  fallback: number,
): number {
  if (lastTimestamp === undefined || !Number.isFinite(lastTimestamp)) return fallback
  return Math.max(fallback, lastTimestamp + timeframeStepSeconds(timeframe))
}

export function reconcileReplayProject(project: ReplayProject, market: MarketMetadata): ReplayProject {
  const baseStep = timeframeStepSeconds(market.base_timeframe)
  const availableStart = market.first_timestamp
  const availableEnd = market.last_timestamp + baseStep
  if (
    !Number.isFinite(availableStart) ||
    !Number.isFinite(availableEnd) ||
    availableEnd <= availableStart
  ) {
    return project
  }

  const boundedFrom = Math.max(project.from, availableStart)
  const boundedTo = Math.min(project.to, availableEnd)
  const hasOverlap = boundedFrom < boundedTo
  const requestedDuration = Math.max(project.to - project.from, timeframeStepSeconds(project.timeframe))
  const availableDuration = availableEnd - availableStart
  const duration = Math.min(requestedDuration, availableDuration)
  const from = hasOverlap ? boundedFrom : availableEnd - duration
  const to = hasOverlap ? boundedTo : availableEnd

  if (from === project.from && to === project.to) return project
  return {
    ...project,
    from,
    to,
    cursorIndex: 0,
    cursorTimestamp: undefined,
    trades: [],
    trendLines: [],
    updatedAt: nowIso(),
  }
}

export function createReplayDraft(metadata: MarketMetadata | null): ReplayProjectDraft {
  const symbol = metadata?.symbol ?? 'BTCUSD'
  const timeframe = metadata?.base_timeframe ?? '1m'
  const start = metadata?.first_timestamp ?? Math.floor(Date.now() / 1000) - 6 * 60 * 60
  const end = metadata
    ? metadata.last_timestamp + timeframeStepSeconds(metadata.base_timeframe)
    : Math.floor(Date.now() / 1000)
  return {
    name: `${symbol} ${timeframe} Replay`,
    symbol,
    timeframe,
    fromInput: toUtcInput(start),
    toInput: toUtcInput(end),
    speed: DEFAULT_SPEED,
  }
}

export function draftFromProject(project: ReplayProject): ReplayProjectDraft {
  return {
    name: project.name,
    symbol: project.symbol,
    timeframe: project.timeframe,
    fromInput: toUtcInput(project.from),
    toInput: toUtcInput(project.to),
    speed: project.speed,
  }
}

export function createReplayProjectFromDraft(
  draft: ReplayProjectDraft,
  existing?: ReplayProject,
): ReplayProject {
  const name = draft.name.trim()
  const symbol = draft.symbol.trim().toUpperCase() || 'BTCUSD'
  const timeframe = draft.timeframe.trim() || '1m'
  const from = fromUtcInput(draft.fromInput)
  const to = fromUtcInput(draft.toInput)

  if (!name) {
    throw new Error('请输入项目名称。')
  }
  if (!Number.isFinite(from) || !Number.isFinite(to)) {
    throw new Error('请选择有效的 UTC 时间。')
  }
  if (from >= to) {
    throw new Error('开始时间必须早于结束时间。')
  }

  const normalizedSpeed = normalizeSpeed(draft.speed)
  const timestamp = nowIso()
  const keepExistingState = Boolean(
    existing &&
    existing.symbol === symbol &&
    existing.timeframe === timeframe &&
    existing.from === from &&
    existing.to === to,
  )

  return {
    id: existing?.id ?? createId(),
    name,
    symbol,
    timeframe,
    from,
    to,
    speed: normalizedSpeed,
    cursorIndex: keepExistingState ? existing?.cursorIndex ?? 0 : 0,
    cursorTimestamp: keepExistingState ? existing?.cursorTimestamp : undefined,
    trades: keepExistingState ? existing?.trades ?? [] : [],
    trendLines: keepExistingState ? existing?.trendLines ?? [] : [],
    createdAt: existing?.createdAt ?? timestamp,
    updatedAt: timestamp,
  }
}

export function createSeedReplayProject(metadata: MarketMetadata): ReplayProject {
  return createReplayProjectFromDraft(createReplayDraft(metadata))
}

export function normalizeReplayProject(raw: unknown): ReplayProject | null {
  if (!raw || typeof raw !== 'object') return null
  const candidate = raw as Partial<ReplayProject>
  if (
    typeof candidate.id !== 'string' ||
    typeof candidate.name !== 'string' ||
    typeof candidate.symbol !== 'string' ||
    typeof candidate.timeframe !== 'string' ||
    typeof candidate.from !== 'number' ||
    typeof candidate.to !== 'number'
  ) {
    return null
  }
  if (!Number.isFinite(candidate.from) || !Number.isFinite(candidate.to) || candidate.from >= candidate.to) {
    return null
  }

  const speed = normalizeSpeed(typeof candidate.speed === 'number' ? candidate.speed : DEFAULT_SPEED)
  const cursorIndex = Math.max(0, Math.floor(typeof candidate.cursorIndex === 'number' ? candidate.cursorIndex : 0))
  const cursorTimestamp = typeof candidate.cursorTimestamp === 'number' && Number.isFinite(candidate.cursorTimestamp)
    ? Math.floor(candidate.cursorTimestamp)
    : undefined
  const trades = Array.isArray(candidate.trades)
    ? sortManualTrades(candidate.trades.map(normalizeManualTrade).filter((trade): trade is ManualTrade => Boolean(trade)))
    : []
  const trendLines = Array.isArray(candidate.trendLines)
    ? candidate.trendLines.map(normalizeTrendLine).filter((line): line is TrendLine => Boolean(line))
    : []
  const createdAt = typeof candidate.createdAt === 'string' ? candidate.createdAt : nowIso()
  const updatedAt = typeof candidate.updatedAt === 'string' ? candidate.updatedAt : createdAt

  return {
    id: candidate.id,
    name: candidate.name,
    symbol: candidate.symbol,
    timeframe: candidate.timeframe,
    from: Math.floor(candidate.from),
    to: Math.floor(candidate.to),
    speed,
    cursorIndex,
    cursorTimestamp,
    trades,
    trendLines,
    createdAt,
    updatedAt,
  }
}

function normalizeTrendLinePoint(raw: unknown): TrendLinePoint | null {
  if (!raw || typeof raw !== 'object') return null
  const candidate = raw as Partial<TrendLinePoint>
  if (
    typeof candidate.timestamp !== 'number' ||
    typeof candidate.price !== 'number' ||
    !Number.isFinite(candidate.timestamp) ||
    !Number.isFinite(candidate.price) ||
    candidate.price <= 0
  ) {
    return null
  }
  return {
    timestamp: Math.floor(candidate.timestamp),
    price: candidate.price,
  }
}

function normalizeTrendLine(raw: unknown): TrendLine | null {
  if (!raw || typeof raw !== 'object') return null
  const candidate = raw as Partial<TrendLine>
  const start = normalizeTrendLinePoint(candidate.start)
  const end = normalizeTrendLinePoint(candidate.end)
  if (
    typeof candidate.id !== 'string' ||
    !start ||
    !end ||
    start.timestamp === end.timestamp
  ) {
    return null
  }
  return {
    id: candidate.id,
    start,
    end,
    createdAt: typeof candidate.createdAt === 'string' ? candidate.createdAt : nowIso(),
  }
}

export function createTrendLine(input: TrendLineInput): TrendLine | null {
  const start = normalizeTrendLinePoint(input.start)
  const end = normalizeTrendLinePoint(input.end)
  if (!start || !end || start.timestamp === end.timestamp) return null
  return {
    id: createId(),
    start,
    end,
    createdAt: nowIso(),
  }
}

function normalizeManualTrade(raw: unknown): ManualTrade | null {
  if (!raw || typeof raw !== 'object') return null
  const candidate = raw as Partial<ManualTrade>
  const action = candidate.action
  if (
    typeof candidate.id !== 'string' ||
    typeof candidate.timestamp !== 'number' ||
    typeof candidate.candleIndex !== 'number' ||
    typeof candidate.price !== 'number' ||
    typeof candidate.quantity !== 'number' ||
    (candidate.side !== 'buy' && candidate.side !== 'sell') ||
    (action !== 'open-long' && action !== 'close-long' && action !== 'open-short' && action !== 'close-short')
  ) {
    return null
  }
  if (
    !Number.isFinite(candidate.timestamp) ||
    !Number.isFinite(candidate.candleIndex) ||
    !Number.isFinite(candidate.price) ||
    !Number.isFinite(candidate.quantity) ||
    candidate.price <= 0 ||
    candidate.quantity <= 0
  ) {
    return null
  }

  return {
    id: candidate.id,
    candleIndex: Math.max(0, Math.floor(candidate.candleIndex)),
    timestamp: Math.floor(candidate.timestamp),
    side: candidate.side,
    action,
    price: candidate.price,
    quantity: candidate.quantity,
    realizedPnl: typeof candidate.realizedPnl === 'number' && Number.isFinite(candidate.realizedPnl)
      ? candidate.realizedPnl
      : null,
    createdAt: typeof candidate.createdAt === 'string' ? candidate.createdAt : nowIso(),
  }
}

export function sortManualTrades(trades: ManualTrade[]): ManualTrade[] {
  return [...trades].sort((left, right) => {
    const candleOrder = left.candleIndex - right.candleIndex
    if (candleOrder !== 0) return candleOrder
    return left.createdAt.localeCompare(right.createdAt)
  })
}

export function summarizeManualTrades(
  trades: ManualTrade[],
  currentPrice: number | null,
  cursorIndex = Number.MAX_SAFE_INTEGER,
): ManualPositionSummary {
  let side: ManualPositionSide = 'flat'
  let quantity = 0
  let averagePrice: number | null = null
  let realizedPnl = 0
  let lastTrade: ManualTrade | null = null

  for (const trade of sortManualTrades(trades).filter((item) => item.candleIndex <= cursorIndex)) {
    const closeQuantity = Math.min(quantity, trade.quantity)
    if (trade.action === 'open-long') {
      const nextQuantity = quantity + trade.quantity
      averagePrice = side === 'long' && averagePrice !== null
        ? ((averagePrice * quantity) + (trade.price * trade.quantity)) / nextQuantity
        : trade.price
      quantity = nextQuantity
      side = 'long'
    }
    if (trade.action === 'close-long' && side === 'long' && averagePrice !== null && closeQuantity > 0) {
      realizedPnl += (trade.price - averagePrice) * closeQuantity
      quantity -= closeQuantity
      if (quantity <= 0) {
        quantity = 0
        averagePrice = null
        side = 'flat'
      }
    }
    if (trade.action === 'open-short') {
      const nextQuantity = quantity + trade.quantity
      averagePrice = side === 'short' && averagePrice !== null
        ? ((averagePrice * quantity) + (trade.price * trade.quantity)) / nextQuantity
        : trade.price
      quantity = nextQuantity
      side = 'short'
    }
    if (trade.action === 'close-short' && side === 'short' && averagePrice !== null && closeQuantity > 0) {
      realizedPnl += (averagePrice - trade.price) * closeQuantity
      quantity -= closeQuantity
      if (quantity <= 0) {
        quantity = 0
        averagePrice = null
        side = 'flat'
      }
    }
    lastTrade = trade
  }

  const validCurrentPrice = typeof currentPrice === 'number' && Number.isFinite(currentPrice)
  const unrealizedPnl = validCurrentPrice && averagePrice !== null
    ? side === 'long'
      ? (currentPrice - averagePrice) * quantity
      : side === 'short'
        ? (averagePrice - currentPrice) * quantity
        : 0
    : 0

  return {
    side,
    quantity,
    averagePrice,
    realizedPnl,
    unrealizedPnl,
    netPnl: realizedPnl + unrealizedPnl,
    tradeCount: trades.filter((item) => item.candleIndex <= cursorIndex).length,
    lastTrade,
  }
}

export function applyManualTrade(project: ReplayProject, input: ManualTradeInput): ReplayProject {
  if (!Number.isFinite(input.candleIndex) || !Number.isFinite(input.timestamp) || !Number.isFinite(input.price) || input.price <= 0) {
    return project
  }

  const candleIndex = Math.max(0, Math.floor(input.candleIndex))
  const retainedTrades = sortManualTrades(project.trades.filter((trade) => trade.candleIndex <= candleIndex))
  const position = summarizeManualTrades(retainedTrades, input.price, candleIndex)
  const quantity = MANUAL_TRADE_QUANTITY
  const action: ManualTradeAction = input.side === 'buy'
    ? position.side === 'short' ? 'close-short' : 'open-long'
    : position.side === 'long' ? 'close-long' : 'open-short'
  const realizedPnl = action === 'close-long' && position.averagePrice !== null
    ? (input.price - position.averagePrice) * quantity
    : action === 'close-short' && position.averagePrice !== null
      ? (position.averagePrice - input.price) * quantity
      : null
  const timestamp = nowIso()

  return {
    ...project,
    cursorIndex: candleIndex,
    cursorTimestamp: Math.floor(input.timestamp),
    trades: sortManualTrades([
      ...retainedTrades,
      {
        id: createId(),
        candleIndex,
        timestamp: Math.floor(input.timestamp),
        side: input.side,
        action,
        price: input.price,
        quantity,
        realizedPnl,
        createdAt: timestamp,
      },
    ]),
    updatedAt: timestamp,
  }
}

export function sortReplayProjects(projects: ReplayProject[]): ReplayProject[] {
  return [...projects].sort((left, right) => {
    const updated = right.updatedAt.localeCompare(left.updatedAt)
    if (updated !== 0) return updated
    return right.createdAt.localeCompare(left.createdAt)
  })
}

export function loadReplayProjects(storage: StorageLike | null = defaultStorage()): ReplayProject[] {
  if (!storage) return []
  try {
    const parsed = JSON.parse(storage.getItem(PROJECTS_KEY) ?? '[]') as unknown
    if (!Array.isArray(parsed)) return []
    return sortReplayProjects(parsed.map(normalizeReplayProject).filter((project): project is ReplayProject => Boolean(project)))
  } catch {
    return []
  }
}

export function saveReplayProjects(projects: ReplayProject[], storage: StorageLike | null = defaultStorage()): void {
  storage?.setItem(PROJECTS_KEY, JSON.stringify(sortReplayProjects(projects)))
}

export function loadActiveReplayProjectId(storage: StorageLike | null = defaultStorage()): string {
  return storage?.getItem(ACTIVE_KEY) ?? ''
}

export function saveActiveReplayProjectId(id: string, storage: StorageLike | null = defaultStorage()): void {
  storage?.setItem(ACTIVE_KEY, id)
}

export function projectRangeLabel(project: ReplayProject): string {
  return `${formatUtc(project.from)} - ${formatUtc(project.to)} UTC`
}

export function replayProgressLabel(project: ReplayProject, totalCandles: number): string {
  if (totalCandles <= 0) return '0 / 0'
  const current = Math.min(project.cursorIndex + 1, totalCandles)
  return `${current} / ${totalCandles}`
}

export function replayProgressPercent(project: ReplayProject, totalCandles: number): number {
  if (totalCandles <= 0) return 0
  return Math.min(((Math.min(project.cursorIndex + 1, totalCandles) / totalCandles) * 100), 100)
}
