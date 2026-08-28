import { describe, expect, it } from 'vitest'
import {
  applyManualTrade,
  clampReplayCursor,
  createReplayProjectFromDraft,
  loadReplayProjects,
  nextReplayWindowStart,
  reconcileReplayProject,
  replayWindowEnd,
  saveReplayProjects,
  summarizeManualTrades,
  timeframeStepSeconds,
  type ReplayProject,
} from './replay'

function createMemoryStorage() {
  const entries = new Map<string, string>()
  return {
    getItem(key: string) {
      return entries.get(key) ?? null
    },
    setItem(key: string, value: string) {
      entries.set(key, value)
    },
  }
}

describe('replay helpers', () => {
  it('normalizes replay projects from draft input', () => {
    const project = createReplayProjectFromDraft({
      name: '  Demo Replay  ',
      symbol: 'btcusd',
      timeframe: '1h',
      fromInput: '2026-08-27T00:00',
      toInput: '2026-08-27T02:00',
      speed: 1.5,
    })

    expect(project).toMatchObject<Partial<ReplayProject>>({
      name: 'Demo Replay',
      symbol: 'BTCUSD',
      timeframe: '1h',
      from: 1787788800,
      to: 1787796000,
      speed: 1.5,
      cursorIndex: 0,
      trades: [],
    })
  })

  it('round-trips stored replay projects', () => {
    const storage = createMemoryStorage()
    const project = createReplayProjectFromDraft({
      name: 'Replay One',
      symbol: 'BTCUSD',
      timeframe: '1m',
      fromInput: '2026-08-27T00:00',
      toInput: '2026-08-27T01:00',
      speed: 2,
    })

    saveReplayProjects([project], storage)
    const projects = loadReplayProjects(storage)

    expect(projects).toHaveLength(1)
    expect(projects[0]).toMatchObject({
      id: project.id,
      name: 'Replay One',
      symbol: 'BTCUSD',
      timeframe: '1m',
      from: project.from,
      to: project.to,
      speed: 2,
      cursorIndex: 0,
      trades: [],
    })
  })

  it('loads old replay projects without manual trades', () => {
    const storage = createMemoryStorage()
    storage.setItem('btcusd.replay.projects.v1', JSON.stringify([{
      id: 'legacy-project',
      name: 'Legacy Replay',
      symbol: 'BTCUSD',
      timeframe: '1m',
      from: 1787788800,
      to: 1787796000,
      speed: 1,
      cursorIndex: 12,
      createdAt: '2026-08-27T00:00:00.000Z',
      updatedAt: '2026-08-27T00:00:00.000Z',
    }]))

    expect(loadReplayProjects(storage)[0]).toMatchObject({
      id: 'legacy-project',
      trades: [],
    })
  })

  it('clamps replay cursors to the available candle range', () => {
    expect(clampReplayCursor(-3, 12)).toBe(0)
    expect(clampReplayCursor(4, 12)).toBe(4)
    expect(clampReplayCursor(99, 12)).toBe(11)
    expect(clampReplayCursor(5, 0)).toBe(0)
  })

  it('creates bounded lazy-loading windows', () => {
    expect(timeframeStepSeconds('1m')).toBe(60)
    expect(replayWindowEnd(1_000, 20_000, '1m')).toBe(15_400)
    expect(nextReplayWindowStart(15_339, '1m', 15_400)).toBe(15_400)
    expect(nextReplayWindowStart(undefined, '1m', 15_400)).toBe(15_400)
  })

  it('moves projects into the current market coverage after a data refresh', () => {
    const project = createReplayProjectFromDraft({
      name: 'Yesterday Replay',
      symbol: 'BTCUSD',
      timeframe: '1m',
      fromInput: '2026-08-27T00:00',
      toInput: '2026-08-27T04:00',
      speed: 1,
    })
    const aligned = reconcileReplayProject(project, {
      symbol: 'BTCUSD',
      base_timeframe: '1m',
      source: 'Parquet',
      candle_count: 241,
      first_timestamp: 2_000,
      last_timestamp: 2_240,
      supported_timeframes: ['1m'],
    })

    expect(aligned).toMatchObject({
      from: 2_000,
      to: 2_240 + 60,
      cursorIndex: 0,
      trades: [],
    })
    expect(aligned).not.toBe(project)
  })

  it('opens and closes long positions with fixed size manual trades', () => {
    const project = createReplayProjectFromDraft({
      name: 'Manual Replay',
      symbol: 'BTCUSD',
      timeframe: '1m',
      fromInput: '2026-08-27T00:00',
      toInput: '2026-08-27T01:00',
      speed: 1,
    })

    const longProject = applyManualTrade(project, {
      side: 'buy',
      candleIndex: 3,
      timestamp: project.from + 180,
      price: 100,
    })
    const closedProject = applyManualTrade(longProject, {
      side: 'sell',
      candleIndex: 5,
      timestamp: project.from + 300,
      price: 112,
    })

    expect(longProject.trades[0]).toMatchObject({
      side: 'buy',
      action: 'open-long',
      quantity: 1,
      realizedPnl: null,
    })
    expect(closedProject.trades[1]).toMatchObject({
      side: 'sell',
      action: 'close-long',
      realizedPnl: 12,
    })
    expect(summarizeManualTrades(closedProject.trades, 112, 5)).toMatchObject({
      side: 'flat',
      quantity: 0,
      realizedPnl: 12,
      unrealizedPnl: 0,
      netPnl: 12,
    })
  })

  it('opens and closes short positions with buy and sell', () => {
    const project = createReplayProjectFromDraft({
      name: 'Short Replay',
      symbol: 'BTCUSD',
      timeframe: '1m',
      fromInput: '2026-08-27T00:00',
      toInput: '2026-08-27T01:00',
      speed: 1,
    })

    const shortProject = applyManualTrade(project, {
      side: 'sell',
      candleIndex: 1,
      timestamp: project.from + 60,
      price: 120,
    })
    const closedProject = applyManualTrade(shortProject, {
      side: 'buy',
      candleIndex: 4,
      timestamp: project.from + 240,
      price: 105,
    })

    expect(shortProject.trades[0]).toMatchObject({
      side: 'sell',
      action: 'open-short',
    })
    expect(summarizeManualTrades(shortProject.trades, 110, 1)).toMatchObject({
      side: 'short',
      quantity: 1,
      unrealizedPnl: 10,
      netPnl: 10,
    })
    expect(closedProject.trades[1]).toMatchObject({
      side: 'buy',
      action: 'close-short',
      realizedPnl: 15,
    })
  })

  it('drops future manual trades when trading after rewinding', () => {
    const project = createReplayProjectFromDraft({
      name: 'Rewind Replay',
      symbol: 'BTCUSD',
      timeframe: '1m',
      fromInput: '2026-08-27T00:00',
      toInput: '2026-08-27T01:00',
      speed: 1,
    })
    const first = applyManualTrade(project, {
      side: 'buy',
      candleIndex: 2,
      timestamp: project.from + 120,
      price: 100,
    })
    const future = applyManualTrade(first, {
      side: 'sell',
      candleIndex: 8,
      timestamp: project.from + 480,
      price: 106,
    })
    const rewritten = applyManualTrade(future, {
      side: 'sell',
      candleIndex: 4,
      timestamp: project.from + 240,
      price: 103,
    })

    expect(rewritten.trades).toHaveLength(2)
    expect(rewritten.trades.map((trade) => trade.candleIndex)).toEqual([2, 4])
    expect(rewritten.trades[1]).toMatchObject({
      action: 'close-long',
      realizedPnl: 3,
    })
  })
})
