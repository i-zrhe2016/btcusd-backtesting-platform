import type { MarketMetadata } from './types'
import { fromUtcInput, toUtcInput, formatUtc } from './format'

export interface ReplayProjectDraft {
  name: string
  symbol: string
  timeframe: string
  fromInput: string
  toInput: string
  speed: number
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

export function createReplayDraft(metadata: MarketMetadata | null): ReplayProjectDraft {
  const symbol = metadata?.symbol ?? 'BTCUSD'
  const timeframe = metadata?.base_timeframe ?? '1m'
  const start = metadata?.first_timestamp ?? Math.floor(Date.now() / 1000) - 6 * 60 * 60
  const end = metadata?.last_timestamp ?? Math.floor(Date.now() / 1000)
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

  return {
    id: existing?.id ?? createId(),
    name,
    symbol,
    timeframe,
    from,
    to,
    speed: normalizedSpeed,
    cursorIndex: existing?.cursorIndex ?? 0,
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
    createdAt,
    updatedAt,
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
