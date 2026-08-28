<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import {
  ArrowLeft,
  ChartCandlestick,
  FolderOpen,
  FolderPlus,
  Pause,
  Play,
  RefreshCw,
  ServerCog,
  SlidersHorizontal,
} from '@lucide/vue'
import MarketChart from './components/MarketChart.vue'
import { getMarketMetadata, getMarketSnapshot } from './api'
import { fromUtcInput, formatNumber, formatUtc } from './format'
import {
  clampReplayCursor,
  createReplayDraft,
  createReplayProjectFromDraft,
  createSeedReplayProject,
  draftFromProject,
  loadActiveReplayProjectId,
  loadReplayProjects,
  nextReplayWindowStart,
  projectRangeLabel,
  reconcileReplayProject,
  replayCursorIndexAtOrBefore,
  replayWindowEnd,
  REPLAY_PAGE_SIZE,
  replayDelayMs,
  replayProgressLabel,
  saveActiveReplayProjectId,
  saveReplayProjects,
  summarizeManualTrades,
  timeframeStepSeconds,
  type ReplayProject,
  type ReplayProjectDraft,
} from './replay'
import type { MarketMetadata, MarketSnapshot } from './types'

type AppView = 'projects' | 'replay'

const viewKey = 'btcusd.workspace.view.v2'
const replayLengths: [number, number, number] = [30, 120, 840]

const timeframeLabels: Record<string, string> = {
  '1m': '1 分',
  '15m': '15 分',
  '30m': '30 分',
  '1h': '1 小时',
  '2h': '2 小时',
  '4h': '4 小时',
  '1d': '日',
  '1w': '周',
  '1M': '月',
}

const metadata = ref<MarketMetadata | null>(null)
const projects = ref<ReplayProject[]>([])
const activeProjectId = ref('')
const replaySnapshot = ref<MarketSnapshot | null>(null)
const replayLoading = ref(true)
const replayError = ref('')
const replayWindowLoading = ref(false)
const replayWindowError = ref('')
const replayNextFrom = ref<number | null>(null)
const notice = ref('')
const pageError = ref('')
const projectError = ref('')
const activeView = ref<AppView>('projects')
const playbackPlaying = ref(false)

const projectDraft = reactive<ReplayProjectDraft>(createReplayDraft(null))

let snapshotRequestId = 0
let playbackTimer: number | null = null

const activeProject = computed(() => projects.value.find((project) => project.id === activeProjectId.value) ?? null)
const totalReplayCandles = computed(() => replaySnapshot.value?.candles.length ?? 0)
const replayHasMore = computed(() => {
  const project = activeProject.value
  return Boolean(project && replayNextFrom.value !== null && replayNextFrom.value < project.to)
})
const activePlaybackIndex = computed(() => clampReplayCursor(activeProject.value?.cursorIndex ?? 0, totalReplayCandles.value))
const chartPlaybackIndex = computed(() => {
  if (activeView.value === 'replay') {
    return activePlaybackIndex.value
  }
  return totalReplayCandles.value > 0 ? totalReplayCandles.value - 1 : 0
})
const currentCandle = computed(() => replaySnapshot.value?.candles[activePlaybackIndex.value] ?? null)
const currentTimeLabel = computed(() => currentCandle.value ? `${formatUtc(currentCandle.value.timestamp)} UTC` : '—')
const currentProjectLabel = computed(() => activeProject.value?.name ?? '未命名项目')
const currentRangeLabel = computed(() => activeProject.value ? projectRangeLabel(activeProject.value) : '—')
const currentProgressLabel = computed(() => activeProject.value ? replayProgressLabel(activeProject.value, totalReplayCandles.value) : '0 / 0')
const replayStatusLabel = computed(() => {
  if (!activeProject.value) return '未选择项目'
  if (replayLoading.value) return '加载中'
  if (replayWindowLoading.value) return '行情加载中'
  if (replayWindowError.value) return '行情加载失败'
  return playbackPlaying.value ? '播放中' : '已暂停'
})
const appStatusLabel = computed(() => {
  if (activeView.value === 'replay') return replayStatusLabel.value
  return '项目库'
})
const activeError = computed(() => {
  if (activeView.value === 'replay') {
    return replayError.value || pageError.value
  }
  return pageError.value
})
const manualTradeStats = computed(() => summarizeManualTrades(
  activeProject.value?.trades ?? [],
  currentCandle.value?.close ?? null,
  activePlaybackIndex.value,
))
const positionLabel = computed(() => {
  const stats = manualTradeStats.value
  if (stats.side === 'flat') return '空仓'
  return `${stats.side === 'long' ? '多' : '空'} ${stats.quantity}`
})
const currentTimeframeValue = computed(() => activeProject.value?.timeframe ?? projectDraft.timeframe)
const timeframeOptions = computed(() => {
  const supported = metadata.value?.supported_timeframes ?? Object.keys(timeframeLabels)
  return supported.filter((value) => value in timeframeLabels)
})
const projectFormError = computed(() => {
  if (!projectDraft.name.trim()) return '请输入项目名称。'
  if (!projectDraft.symbol.trim()) return '请输入品种。'
  if (!projectDraft.timeframe.trim()) return '请选择周期。'
  const from = fromUtcInput(projectDraft.fromInput)
  const to = fromUtcInput(projectDraft.toInput)
  if (!Number.isFinite(from) || !Number.isFinite(to)) return '请选择有效的 UTC 时间。'
  if (from >= to) return '开始时间必须早于结束时间。'
  return ''
})
const projectActionLabel = computed(() => activeProject.value ? '保存并打开' : '创建并打开')

function loadAppView(): AppView {
  if (typeof window === 'undefined') return 'projects'
  const value = window.localStorage.getItem(viewKey)
  return value === 'replay' || value === 'projects' ? value : 'projects'
}

function saveAppView(value: AppView) {
  if (typeof window === 'undefined') return
  window.localStorage.setItem(viewKey, value)
}

function setNotice(message: string) {
  notice.value = message
}

function syncDraftFromProject(project: ReplayProject) {
  Object.assign(projectDraft, draftFromProject(project))
}

function persistProjectsState() {
  saveReplayProjects(projects.value)
  saveActiveReplayProjectId(activeProjectId.value)
}

function updateProject(project: ReplayProject) {
  projects.value = projects.value.map((current) => (current.id === project.id ? project : current))
  persistProjectsState()
}

function alignProjectToMarket(project: ReplayProject): { project: ReplayProject; changed: boolean } {
  if (!metadata.value) return { project, changed: false }
  const aligned = reconcileReplayProject(project, metadata.value)
  if (aligned === project) return { project, changed: false }
  projects.value = projects.value.map((current) => (current.id === project.id ? aligned : current))
  saveReplayProjects(projects.value)
  return { project: aligned, changed: true }
}

function updateProjectCursor(projectId: string, cursorIndex: number, cursorTimestamp?: number) {
  const current = projects.value.find((project) => project.id === projectId)
  if (!current) return
  updateProject({
    ...current,
    cursorIndex,
    cursorTimestamp: typeof cursorTimestamp === 'number' && Number.isFinite(cursorTimestamp)
      ? Math.floor(cursorTimestamp)
      : current.cursorTimestamp,
    updatedAt: new Date().toISOString(),
  })
}

function updateActiveProjectSpeed(speed: number) {
  if (!activeProject.value) return
  const normalized = Number.isFinite(speed) ? speed : 1
  projectDraft.speed = normalized
  updateProject({
    ...activeProject.value,
    speed: normalized,
    updatedAt: new Date().toISOString(),
  })
}

function clearPlaybackTimer() {
  if (playbackTimer !== null) {
    window.clearInterval(playbackTimer)
    playbackTimer = null
  }
}

function pausePlayback() {
  playbackPlaying.value = false
}

function setPlaybackCursor(index: number, shouldPause = true) {
  const project = activeProject.value
  const total = totalReplayCandles.value
  if (!project || total <= 0) return
  const cursorIndex = clampReplayCursor(index, total)
  updateProjectCursor(project.id, cursorIndex, replaySnapshot.value?.candles[cursorIndex]?.timestamp)
  if (shouldPause) {
    pausePlayback()
  }
}

function changeReplayTimeframe(event: Event) {
  const project = activeProject.value
  const timeframe = (event.target as HTMLSelectElement).value
  if (!project || !timeframe || project.timeframe === timeframe) return

  pausePlayback()
  projectDraft.timeframe = timeframe
  const cursorTimestamp = currentCandle.value?.timestamp ?? project.cursorTimestamp

  const defaultProjectName = `${project.symbol} ${project.timeframe} Replay`
  const nextDraft: ReplayProjectDraft = {
    ...draftFromProject(project),
    name: project.name === defaultProjectName ? `${project.symbol} ${timeframe} Replay` : project.name,
    timeframe,
  }
  const nextProject = {
    ...createReplayProjectFromDraft(nextDraft, project),
    cursorIndex: 0,
    cursorTimestamp,
  }

  updateProject(nextProject)
  syncDraftFromProject(nextProject)
  void loadReplaySnapshot(nextProject, {
    cursorTimestamp,
    notice: cursorTimestamp
      ? `周期已切换为 ${timeframeLabels[timeframe] ?? timeframe}，已保留 ${formatUtc(cursorTimestamp)} UTC 的复盘进度；旧周期交易记录已清空。`
      : `周期已切换为 ${timeframeLabels[timeframe] ?? timeframe}，旧周期交易记录已清空。`,
  })
}

function openProjects() {
  pausePlayback()
  activeView.value = 'projects'
}

function openReplay() {
  if (!activeProject.value) {
    activeView.value = 'projects'
    return
  }
  activeView.value = 'replay'
}

function togglePlayback() {
  if (!activeProject.value || !replaySnapshot.value?.candles.length) return
  playbackPlaying.value = !playbackPlaying.value
}

function createProject() {
  projectError.value = ''
  try {
    const project = createReplayProjectFromDraft(projectDraft)
    projects.value = [project, ...projects.value.filter((item) => item.id !== project.id)]
    activeProjectId.value = project.id
    saveActiveReplayProjectId(project.id)
    saveReplayProjects(projects.value)
    syncDraftFromProject(project)
    playbackPlaying.value = false
    activeView.value = 'replay'
    void loadReplaySnapshot(project)
    setNotice(`项目“${project.name}”已创建。`)
  } catch (caught) {
    projectError.value = caught instanceof Error ? caught.message : '项目创建失败。'
  }
}

function saveActiveProject() {
  const project = activeProject.value
  if (!project) {
    createProject()
    return
  }
  projectError.value = ''
  try {
    const next = createReplayProjectFromDraft(projectDraft, project)
    projects.value = projects.value.map((item) => (item.id === next.id ? next : item))
    activeProjectId.value = next.id
    saveActiveReplayProjectId(next.id)
    saveReplayProjects(projects.value)
    syncDraftFromProject(next)
    playbackPlaying.value = false
    activeView.value = 'replay'
    void loadReplaySnapshot(next)
    setNotice(`项目“${next.name}”已保存。`)
  } catch (caught) {
    projectError.value = caught instanceof Error ? caught.message : '项目保存失败。'
  }
}

function selectProject(projectId: string) {
  const storedProject = projects.value.find((item) => item.id === projectId)
  if (!storedProject) return
  const { project, changed } = alignProjectToMarket(storedProject)
  projectError.value = ''
  activeProjectId.value = project.id
  saveActiveReplayProjectId(project.id)
  syncDraftFromProject(project)
  playbackPlaying.value = false
  activeView.value = 'replay'
  void loadReplaySnapshot(project)
  if (changed) setNotice('项目时间范围已调整到当前可用行情，旧区间交易记录已清除。')
}

function mergeMarketSnapshots(current: MarketSnapshot, incoming: MarketSnapshot): MarketSnapshot {
  const candles = new Map(current.candles.map((candle) => [candle.timestamp, candle]))
  for (const candle of incoming.candles) {
    candles.set(candle.timestamp, candle)
  }
  return {
    ...current,
    candles: [...candles.values()].sort((left, right) => left.timestamp - right.timestamp),
  }
}

function nextReplayRequestFrom(snapshot: MarketSnapshot, project: ReplayProject, requestedTo: number): number | null {
  const nextFrom = nextReplayWindowStart(
    snapshot.candles.at(-1)?.timestamp,
    project.timeframe,
    requestedTo,
  )
  return nextFrom < project.to ? nextFrom : null
}

async function loadReplaySnapshot(
  project: ReplayProject,
  options: { cursorTimestamp?: number; notice?: string } = {},
) {
  const requestId = ++snapshotRequestId
  replayLoading.value = true
  replayError.value = ''
  replayWindowLoading.value = false
  replayWindowError.value = ''
  replayNextFrom.value = null
  replaySnapshot.value = null

  try {
    const step = timeframeStepSeconds(project.timeframe)
    const requestedCursorTimestamp = options.cursorTimestamp ?? project.cursorTimestamp
    const hasCursorTimestamp = typeof requestedCursorTimestamp === 'number' && Number.isFinite(requestedCursorTimestamp)
    const boundedCursorTimestamp = hasCursorTimestamp
      ? Math.min(Math.max(requestedCursorTimestamp as number, project.from), Math.max(project.from, project.to - step))
      : null
    const requestedFrom = boundedCursorTimestamp === null
      ? project.from
      : Math.max(project.from, boundedCursorTimestamp - (step * Math.floor(REPLAY_PAGE_SIZE / 2)))
    const requestedTo = boundedCursorTimestamp === null
      ? replayWindowEnd(project.from, project.to, project.timeframe)
      : Math.min(project.to, boundedCursorTimestamp + (step * (REPLAY_PAGE_SIZE - Math.floor(REPLAY_PAGE_SIZE / 2))))
    const snapshot = await getMarketSnapshot({
      timeframe: project.timeframe,
      from: requestedFrom,
      to: requestedTo,
      limit: REPLAY_PAGE_SIZE,
      lengths: replayLengths,
    })
    if (requestId !== snapshotRequestId) return
    replaySnapshot.value = snapshot
    replayNextFrom.value = nextReplayRequestFrom(snapshot, project, requestedTo)
    if (snapshot.candles.length) {
      const clamped = boundedCursorTimestamp === null
        ? clampReplayCursor(project.cursorIndex, snapshot.candles.length)
        : replayCursorIndexAtOrBefore(snapshot.candles, boundedCursorTimestamp)
      const mappedTimestamp = snapshot.candles[clamped]?.timestamp
      if (clamped !== project.cursorIndex || mappedTimestamp !== project.cursorTimestamp) {
        updateProjectCursor(project.id, clamped, mappedTimestamp)
      }
    }
    setNotice(options.notice ?? `项目“${project.name}”已加载 ${snapshot.candles.length} 根 ${project.timeframe} K 线。`)
  } catch (caught) {
    if (requestId !== snapshotRequestId) return
    replayError.value = caught instanceof Error ? caught.message : '复盘行情加载失败。'
  } finally {
    if (requestId === snapshotRequestId) {
      replayLoading.value = false
    }
  }
}

async function loadMoreReplay() {
  const project = activeProject.value
  const from = replayNextFrom.value
  if (!project || from === null || from >= project.to || replayWindowLoading.value) return

  const requestId = snapshotRequestId
  const projectId = project.id
  const requestedTo = replayWindowEnd(from, project.to, project.timeframe)
  replayWindowLoading.value = true
  replayWindowError.value = ''

  try {
    const snapshot = await getMarketSnapshot({
      timeframe: project.timeframe,
      from,
      to: requestedTo,
      limit: REPLAY_PAGE_SIZE,
      lengths: replayLengths,
    })
    if (requestId !== snapshotRequestId || activeProject.value?.id !== projectId) return
    replaySnapshot.value = replaySnapshot.value
      ? mergeMarketSnapshots(replaySnapshot.value, snapshot)
      : snapshot
    replayNextFrom.value = nextReplayRequestFrom(snapshot, project, requestedTo)
    setNotice(`按需加载了 ${snapshot.candles.length} 根 ${project.timeframe} K 线。`)
  } catch (caught) {
    if (requestId === snapshotRequestId && activeProject.value?.id === projectId) {
      replayWindowError.value = caught instanceof Error ? caught.message : '更多行情加载失败。'
    }
  } finally {
    if (requestId === snapshotRequestId && activeProject.value?.id === projectId) {
      replayWindowLoading.value = false
    }
  }
}

async function initialize() {
  pageError.value = ''
  try {
    metadata.value = await getMarketMetadata()
    activeView.value = loadAppView()

    const storedProjects = loadReplayProjects()
    if (!storedProjects.length && metadata.value) {
      const seed = createSeedReplayProject(metadata.value)
      projects.value = [seed]
      activeProjectId.value = seed.id
      saveReplayProjects(projects.value)
      saveActiveReplayProjectId(seed.id)
      syncDraftFromProject(seed)
      await loadReplaySnapshot(seed)
      return
    }

    projects.value = storedProjects
    const storedActive = loadActiveReplayProjectId()
    const storedActiveProject = projects.value.find((project) => project.id === storedActive) ?? projects.value[0]
    const alignedActiveProject = storedActiveProject
      ? alignProjectToMarket(storedActiveProject)
      : { project: storedActiveProject, changed: false }
    const active = alignedActiveProject.project
    activeProjectId.value = active?.id ?? ''
    if (activeProjectId.value) {
      saveActiveReplayProjectId(activeProjectId.value)
    }
    if (active) {
      syncDraftFromProject(active)
      await loadReplaySnapshot(active)
      if (alignedActiveProject.changed) {
        setNotice('项目时间范围已调整到当前可用行情，旧区间交易记录已清除。')
      }
    } else {
      activeView.value = 'projects'
      Object.assign(projectDraft, createReplayDraft(metadata.value))
    }
  } catch (caught) {
    pageError.value = caught instanceof Error ? caught.message : '平台初始化失败。'
    replayLoading.value = false
  }
}

function speedToText(speed: number): string {
  const value = Number.isFinite(speed) ? speed : 1
  return `x${value % 1 === 0 ? value.toFixed(0) : value.toFixed(2)}`
}

watch(activeView, (value) => {
  saveAppView(value)
  if (value !== 'replay') {
    playbackPlaying.value = false
  }
})

watch([playbackPlaying, () => activeProject.value?.speed, () => activeProjectId.value, () => totalReplayCandles.value], () => {
  clearPlaybackTimer()
  const project = activeProject.value
  if (activeView.value !== 'replay' || !playbackPlaying.value || !project || !replaySnapshot.value?.candles.length) return
  playbackTimer = window.setInterval(() => {
    const current = activeProject.value
    const total = totalReplayCandles.value
    if (!current || total <= 0) {
      pausePlayback()
      return
    }
    const nextIndex = current.cursorIndex + 1
    if (nextIndex >= total) {
      if (replayHasMore.value) {
        void loadMoreReplay()
        return
      }
      pausePlayback()
      return
    }
    setPlaybackCursor(nextIndex, false)
  }, replayDelayMs(project.speed))
}, { immediate: true })

onMounted(initialize)

onBeforeUnmount(() => {
  clearPlaybackTimer()
})
</script>

<template>
  <div class="app-shell" :class="{ 'replay-active': activeView === 'replay' }">
    <header class="topbar">
      <a class="brand" href="/" aria-label="BTCUSD 复盘平台首页">
        <span class="brand-mark" aria-hidden="true"><ChartCandlestick :size="20" /></span>
        <span>
          <strong>BTCUSD</strong>
          <small>REPLAY WORKSPACE</small>
        </span>
      </a>

      <div class="mode-switch" role="tablist" aria-label="工作区模式">
        <button
          type="button"
          class="mode-chip"
          :class="{ active: activeView === 'projects' }"
          :aria-pressed="activeView === 'projects'"
          @click="openProjects"
        >
          项目
        </button>
        <button
          type="button"
          class="mode-chip"
          :class="{ active: activeView === 'replay' }"
          :aria-pressed="activeView === 'replay'"
          :disabled="!activeProject"
          @click="openReplay"
        >
          复盘
        </button>
      </div>

      <div class="service-state">
        <span class="status-dot" aria-hidden="true" />
        <span>{{ appStatusLabel }}</span>
      </div>
    </header>

    <main id="main-content" class="workspace" :class="`workspace-${activeView}`" tabindex="-1">
      <p class="sr-only" aria-live="polite">{{ notice }}</p>

      <template v-if="activeView === 'projects'">
        <section class="workspace-head" aria-labelledby="page-title">
          <div class="workspace-copy">
            <p class="eyebrow">PROJECT LIBRARY / UTC</p>
            <h1 id="page-title">复盘项目库</h1>
            <p>先创建或打开项目，随后进入独立的全屏 K 线复盘窗口。</p>
          </div>
          <dl class="workspace-stats">
            <div>
              <dt>当前项目</dt>
              <dd>{{ currentProjectLabel }}</dd>
            </div>
            <div>
              <dt>时间范围</dt>
              <dd>{{ currentRangeLabel }}</dd>
            </div>
            <div>
              <dt>当前进度</dt>
              <dd>{{ currentProgressLabel }}</dd>
            </div>
            <div>
              <dt>手动交易</dt>
              <dd>{{ activeProject?.trades.length ?? 0 }}</dd>
            </div>
          </dl>
        </section>

        <div v-if="activeError" class="alert error-alert" role="alert">
          <ServerCog :size="20" aria-hidden="true" />
          <span>{{ activeError }}</span>
          <button type="button" class="text-button" @click="initialize">重试</button>
        </div>

        <div class="project-layout">
          <section class="panel project-form-panel" aria-labelledby="project-form-title">
            <div class="panel-head">
              <div>
                <p class="eyebrow">SETUP</p>
                <h2 id="project-form-title">新建项目</h2>
              </div>
              <FolderPlus :size="22" class="heading-icon" aria-hidden="true" />
            </div>

            <form class="project-form" @submit.prevent="saveActiveProject">
              <label>
                项目名称
                <input v-model="projectDraft.name" type="text" maxlength="120" autocomplete="off" />
              </label>
              <div class="field-grid two">
                <label>
                  品种
                  <input v-model="projectDraft.symbol" type="text" autocomplete="off" />
                </label>
                <label>
                  周期
                  <select v-model="projectDraft.timeframe">
                    <option v-for="timeframe in timeframeOptions" :key="timeframe" :value="timeframe">
                      {{ timeframeLabels[timeframe] ?? timeframe }}
                    </option>
                  </select>
                </label>
              </div>
              <div class="field-grid two">
                <label>
                  开始时间（UTC）
                  <input v-model="projectDraft.fromInput" type="datetime-local" />
                </label>
                <label>
                  结束时间（UTC）
                  <input v-model="projectDraft.toInput" type="datetime-local" />
                </label>
              </div>
              <div class="field-grid two speed-field">
                <label>
                  播放速度
                  <input v-model.number="projectDraft.speed" type="number" min="0.25" max="8" step="0.25" />
                </label>
                <div class="speed-hint">
                  <span class="speed-icon"><SlidersHorizontal :size="18" aria-hidden="true" /></span>
                  <span>{{ speedToText(projectDraft.speed) }}</span>
                </div>
              </div>
              <p class="form-error" :class="{ hidden: !projectFormError }" role="alert">{{ projectFormError || ' ' }}</p>
              <p v-if="projectError" class="form-error" role="alert">{{ projectError }}</p>
              <div class="form-actions">
                <button class="primary-button" type="submit" :disabled="Boolean(projectFormError)">
                  <RefreshCw v-if="activeProject" :size="18" aria-hidden="true" />
                  <FolderPlus v-else :size="18" aria-hidden="true" />
                  {{ projectActionLabel }}
                </button>
                <button class="secondary-button" type="button" :disabled="Boolean(projectFormError)" @click="createProject">
                  新建并打开
                </button>
              </div>
            </form>
          </section>

          <section class="panel project-list-panel" aria-labelledby="project-list-title">
            <div class="panel-head">
              <div>
                <p class="eyebrow">PROJECTS</p>
                <h2 id="project-list-title">已保存项目</h2>
              </div>
              <FolderOpen :size="22" class="heading-icon" aria-hidden="true" />
            </div>

            <div v-if="projects.length" class="project-list">
              <button
                v-for="project in projects"
                :key="project.id"
                type="button"
                class="project-item"
                :class="{ active: project.id === activeProjectId }"
                @click="selectProject(project.id)"
              >
                <div class="project-item-head">
                  <strong>{{ project.name }}</strong>
                  <span class="status-pill" :data-status="project.id === activeProjectId ? 'running' : 'idle'">
                    {{ project.id === activeProjectId ? '当前' : '已保存' }}
                  </span>
                </div>
                <span>{{ timeframeLabels[project.timeframe] ?? project.timeframe }} · {{ projectRangeLabel(project) }}</span>
                <span>游标 {{ project.cursorIndex + 1 }} · 交易 {{ project.trades.length }} · {{ speedToText(project.speed) }}</span>
              </button>
            </div>
            <p v-else class="history-empty">暂无项目。填写左侧表单后会直接打开全屏 K 线复盘窗口。</p>
          </section>
        </div>
      </template>

      <section v-else-if="activeView === 'replay'" class="replay-screen" aria-labelledby="replay-title">
        <h1 id="replay-title" class="sr-only">{{ currentProjectLabel }} 复盘</h1>
        <MarketChart
          class="fullscreen-chart"
          :snapshot="replaySnapshot"
          :loading="replayLoading"
          :playback-index="chartPlaybackIndex"
          :playing="playbackPlaying"
          :project-name="currentProjectLabel"
          :show-header="false"
          :manual-trades="activeProject?.trades ?? []"
        />

        <div class="replay-topline">
          <button class="secondary-button replay-back-button" type="button" @click="openProjects">
            <ArrowLeft :size="18" aria-hidden="true" />
            项目库
          </button>
          <div class="replay-status">
            <span class="status-chip" :data-status="playbackPlaying ? 'running' : 'paused'">{{ replayStatusLabel }}</span>
            <span class="status-chip muted">{{ currentProgressLabel }}</span>
            <span class="status-chip muted">{{ positionLabel }}</span>
          </div>
        </div>

        <div v-if="activeError" class="alert replay-alert" role="alert">
          <ServerCog :size="20" aria-hidden="true" />
          <span>{{ activeError }}</span>
          <button type="button" class="text-button" @click="initialize">重试</button>
        </div>

        <div class="replay-console" aria-label="复盘状态栏">
          <div class="console-strip">
            <div class="console-summary">
              <strong>{{ currentProjectLabel }}</strong>
              <span>{{ currentRangeLabel }}</span>
            </div>

            <label class="timeframe-control">
              <span>周期</span>
              <select
                :value="currentTimeframeValue"
                :disabled="!activeProject || replayLoading"
                @change="changeReplayTimeframe"
              >
                <option v-for="timeframe in timeframeOptions" :key="timeframe" :value="timeframe">
                  {{ timeframeLabels[timeframe] ?? timeframe }}
                </option>
              </select>
            </label>

            <button class="primary-button console-play" type="button" :disabled="!totalReplayCandles" @click="togglePlayback">
              <Pause v-if="playbackPlaying" :size="18" aria-hidden="true" />
              <Play v-else :size="18" aria-hidden="true" />
              {{ playbackPlaying ? '暂停' : '播放' }}
            </button>

            <div class="console-quick-stats" aria-label="当前复盘状态">
              <span>{{ currentProgressLabel }}</span>
              <span>{{ currentTimeLabel }}</span>
              <span>{{ positionLabel }}</span>
              <span v-if="replayWindowLoading">行情加载中</span>
              <span v-else-if="replayWindowError" :title="replayWindowError">行情加载失败</span>
            </div>
          </div>
        </div>
      </section>
    </main>
  </div>
</template>
