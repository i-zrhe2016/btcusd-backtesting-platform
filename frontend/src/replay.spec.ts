import { describe, expect, it } from 'vitest'
import {
  clampReplayCursor,
  createReplayProjectFromDraft,
  loadReplayProjects,
  saveReplayProjects,
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
    })
  })

  it('clamps replay cursors to the available candle range', () => {
    expect(clampReplayCursor(-3, 12)).toBe(0)
    expect(clampReplayCursor(4, 12)).toBe(4)
    expect(clampReplayCursor(99, 12)).toBe(11)
    expect(clampReplayCursor(5, 0)).toBe(0)
  })
})
