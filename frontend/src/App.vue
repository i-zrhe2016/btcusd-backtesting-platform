<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'
import {
  ChartCandlestick,
  ChevronLeft,
  ChevronRight,
  ChevronsLeft,
  ChevronsRight,
  Clock3,
  FolderOpen,
  FolderPlus,
  Gauge,
  Pause,
  Play,
  RefreshCw,
  ServerCog,
  SlidersHorizontal,
  TimerReset,
} from '@lucide/vue'
import BacktestPanel from './components/BacktestPanel.vue'
import HistoryPanel from './components/HistoryPanel.vue'
import MarketChart from './components/MarketChart.vue'
import { createBacktest, getMarketMetadata, getMarketSnapshot, listBacktests } from './api'
import { fromUtcInput, formatNumber, formatUtc } from './format'
import {
  clampReplayCursor,
  createReplayDraft,
  createReplayProjectFromDraft,
  createSeedReplayProject,
  draftFromProject,
  loadActiveReplayProjectId,
  loadReplayProjects,
  projectRangeLabel,
  replayDelayMs,
  replayProgressLabel,
  replayProgressPercent,
  saveActiveReplayProjectId,
  saveReplayProjects,
  type ReplayProject,
  type ReplayProjectDraft,
} from './replay'
import type { BacktestRecord, BacktestRequest, MarketMetadata, MarketSnapshot } from './types'

type WorkspaceMode = 'replay' | 'backtest'

const modeKey = 'btcusd.workspace.mode.v1'
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
const history = ref<BacktestRecord[]>([])
const lastRecord = ref<BacktestRecord | null>(null)
const historyLoading = ref(true)
const backtestRunning = ref(false)
const backtestError = ref('')
const notice = ref('')
const pageError = ref('')
const projectError = ref('')
const workspaceMode = ref<WorkspaceMode>('replay')
const playbackPlaying = ref(false)

const projectDraft = reactive<ReplayProjectDraft>(createReplayDraft(null))

let snapshotRequestId = 0
let playbackTimer: number | null = null

const activeProject = computed(() => projects.value.find((project) => project.id === activeProjectId.value) ?? null)
const totalReplayCandles = computed(() => replaySnapshot.value?.candles.length ?? 0)
const activePlaybackIndex = computed(() => clampReplayCursor(activeProject.value?.cursorIndex ?? 0, totalReplayCandles.value))
const chartPlaybackIndex = computed(() => {
  if (workspaceMode.value === 'replay') {
    return activePlaybackIndex.value
  }
  return totalReplayCandles.value > 0 ? totalReplayCandles.value - 1 : 0
})
const currentCandle = computed(() => replaySnapshot.value?.candles[activePlaybackIndex.value] ?? null)
const currentTimeLabel = computed(() => currentCandle.value ? `${formatUtc(currentCandle.value.timestamp)} UTC` : '—')
const currentProjectLabel = computed(() => activeProject.value?.name ?? '未命名项目')
const currentRangeLabel = computed(() => activeProject.value ? projectRangeLabel(activeProject.value) : '—')
const currentProgressLabel = computed(() => activeProject.value ? replayProgressLabel(activeProject.value, totalReplayCandles.value) : '0 / 0')
const currentProgressPercent = computed(() => activeProject.value ? replayProgressPercent(activeProject.value, totalReplayCandles.value) : 0)
const currentSpeedLabel = computed(() => `x${(activeProject.value?.speed ?? 1).toFixed((activeProject.value?.speed ?? 1) % 1 === 0 ? 0 : 2)}`)
const replayStatusLabel = computed(() => {
  if (!activeProject.value) return '未选择项目'
  return playbackPlaying.value ? '播放中' : '已暂停'
})
const activeError = computed(() => {
  if (workspaceMode.value === 'replay') {
    return replayError.value || backtestError.value || pageError.value
  }
  return backtestError.value || replayError.value || pageError.value
})
const activeProjectTimeframe = computed(() => activeProject.value?.timeframe ?? metadata.value?.base_timeframe ?? '1m')
const activeProjectFrom = computed(() => activeProject.value?.from ?? metadata.value?.first_timestamp ?? 0)
const activeProjectTo = computed(() => activeProject.value?.to ?? metadata.value?.last_timestamp ?? 0)
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
const projectActionLabel = computed(() => activeProject.value ? '保存当前项目' : '创建项目')

function loadWorkspaceMode(): WorkspaceMode {
  if (typeof window === 'undefined') return 'replay'
  return window.localStorage.getItem(modeKey) === 'backtest' ? 'backtest' : 'replay'
}

function saveWorkspaceMode(value: WorkspaceMode) {
  if (typeof window === 'undefined') return
  window.localStorage.setItem(modeKey, value)
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

function updateProjectCursor(projectId: string, cursorIndex: number) {
  const current = projects.value.find((project) => project.id === projectId)
  if (!current) return
  updateProject({
    ...current,
    cursorIndex,
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
  updateProjectCursor(project.id, cursorIndex)
  if (shouldPause) {
    pausePlayback()
  }
}

function seekReplay(event: Event) {
  const value = Number((event.target as HTMLInputElement).value)
  setPlaybackCursor(value, true)
}

function changePlaybackSpeed(event: Event) {
  updateActiveProjectSpeed((event.target as HTMLInputElement).valueAsNumber)
}

function stepReplay(delta: number) {
  setPlaybackCursor(activePlaybackIndex.value + delta, true)
}

function seekToStart() {
  setPlaybackCursor(0, true)
}

function seekToEnd() {
  const total = totalReplayCandles.value
  if (!total) return
  setPlaybackCursor(total - 1, true)
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
    void loadReplaySnapshot(next)
    setNotice(`项目“${next.name}”已保存。`)
  } catch (caught) {
    projectError.value = caught instanceof Error ? caught.message : '项目保存失败。'
  }
}

function selectProject(projectId: string) {
  const project = projects.value.find((item) => item.id === projectId)
  if (!project) return
  projectError.value = ''
  activeProjectId.value = project.id
  saveActiveReplayProjectId(project.id)
  syncDraftFromProject(project)
  playbackPlaying.value = false
  void loadReplaySnapshot(project)
}

async function loadReplaySnapshot(project: ReplayProject) {
  const requestId = ++snapshotRequestId
  replayLoading.value = true
  replayError.value = ''
  replaySnapshot.value = null

  try {
    const snapshot = await getMarketSnapshot({
      timeframe: project.timeframe,
      from: project.from,
      to: project.to,
      lengths: replayLengths,
    })
    if (requestId !== snapshotRequestId) return
    replaySnapshot.value = snapshot
    const clamped = clampReplayCursor(project.cursorIndex, snapshot.candles.length)
    if (clamped !== project.cursorIndex) {
      updateProjectCursor(project.id, clamped)
    }
    setNotice(`项目“${project.name}”已加载 ${snapshot.candles.length} 根 ${project.timeframe} K 线。`)
  } catch (caught) {
    if (requestId !== snapshotRequestId) return
    replayError.value = caught instanceof Error ? caught.message : '复盘行情加载失败。'
  } finally {
    if (requestId === snapshotRequestId) {
      replayLoading.value = false
    }
  }
}

async function loadHistory() {
  historyLoading.value = true
  backtestError.value = ''
  try {
    history.value = await listBacktests()
  } catch (caught) {
    backtestError.value = caught instanceof Error ? caught.message : '回测记录加载失败。'
  } finally {
    historyLoading.value = false
  }
}

async function runBacktest(request: BacktestRequest) {
  backtestRunning.value = true
  backtestError.value = ''
  setNotice('回测已提交，正在计算。')
  try {
    lastRecord.value = await createBacktest(request)
    setNotice(`回测“${lastRecord.value.name}”已完成。`)
    await loadHistory()
  } catch (caught) {
    backtestError.value = caught instanceof Error ? caught.message : '回测执行失败。'
  } finally {
    backtestRunning.value = false
  }
}

async function initialize() {
  pageError.value = ''
  try {
    metadata.value = await getMarketMetadata()
    workspaceMode.value = loadWorkspaceMode()

    const storedProjects = loadReplayProjects()
    if (!storedProjects.length && metadata.value) {
      const seed = createSeedReplayProject(metadata.value)
      projects.value = [seed]
      activeProjectId.value = seed.id
      saveReplayProjects(projects.value)
      saveActiveReplayProjectId(seed.id)
      syncDraftFromProject(seed)
      await Promise.all([loadReplaySnapshot(seed), loadHistory()])
      return
    }

    projects.value = storedProjects
    const storedActive = loadActiveReplayProjectId()
    activeProjectId.value = projects.value.some((project) => project.id === storedActive)
      ? storedActive
      : projects.value[0]?.id ?? ''
    if (activeProjectId.value) {
      saveActiveReplayProjectId(activeProjectId.value)
    }
    const active = activeProject.value ?? projects.value[0]
    if (active) {
      syncDraftFromProject(active)
      await Promise.all([loadReplaySnapshot(active), loadHistory()])
    } else {
      Object.assign(projectDraft, createReplayDraft(metadata.value))
      await loadHistory()
    }
  } catch (caught) {
    pageError.value = caught instanceof Error ? caught.message : '平台初始化失败。'
    replayLoading.value = false
    historyLoading.value = false
  }
}

function speedToText(speed: number): string {
  const value = Number.isFinite(speed) ? speed : 1
  return `x${value % 1 === 0 ? value.toFixed(0) : value.toFixed(2)}`
}

watch(workspaceMode, (value) => {
  saveWorkspaceMode(value)
  if (value !== 'replay') {
    playbackPlaying.value = false
  }
})

watch(() => projectDraft.speed, (value) => {
  if (!activeProject.value) return
  if (Number.isFinite(value) && value !== activeProject.value.speed) {
    updateActiveProjectSpeed(value)
  }
})

watch([playbackPlaying, () => activeProject.value?.speed, () => activeProjectId.value, () => totalReplayCandles.value], () => {
  clearPlaybackTimer()
  const project = activeProject.value
  if (!playbackPlaying.value || !project || !replaySnapshot.value?.candles.length) return
  playbackTimer = window.setInterval(() => {
    const current = activeProject.value
    const total = totalReplayCandles.value
    if (!current || total <= 0) {
      pausePlayback()
      return
    }
    const nextIndex = current.cursorIndex + 1
    if (nextIndex >= total) {
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
  <div class="app-shell">
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
          :class="{ active: workspaceMode === 'replay' }"
          :aria-pressed="workspaceMode === 'replay'"
          @click="workspaceMode = 'replay'"
        >
          复盘
        </button>
        <button
          type="button"
          class="mode-chip"
          :class="{ active: workspaceMode === 'backtest' }"
          :aria-pressed="workspaceMode === 'backtest'"
          @click="workspaceMode = 'backtest'"
        >
          回测
        </button>
      </div>

      <div class="service-state">
        <span class="status-dot" aria-hidden="true" />
        <span>{{ workspaceMode === 'replay' ? replayStatusLabel : '策略回测' }}</span>
      </div>
    </header>

    <main id="main-content" class="workspace" tabindex="-1">
      <section class="workspace-head" aria-labelledby="page-title">
        <div class="workspace-copy">
          <p class="eyebrow">UTC / BTCUSD</p>
          <h1 id="page-title">
            {{ workspaceMode === 'replay' ? 'Forex Tester 式复盘工作台' : '策略回测工作台' }}
          </h1>
          <p>
            {{ workspaceMode === 'replay' ? '新建项目、设定时间、逐根播放或暂停 K 线。' : '沿用当前项目时间范围运行回测并查看历史记录。' }}
          </p>
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
            <dt>当前时间</dt>
            <dd>{{ currentTimeLabel }}</dd>
          </div>
        </dl>
      </section>

      <div v-if="activeError" class="alert error-alert" role="alert">
        <ServerCog :size="20" aria-hidden="true" />
        <span>{{ activeError }}</span>
        <button type="button" class="text-button" @click="initialize">重试</button>
      </div>

      <p class="sr-only" aria-live="polite">{{ notice }}</p>

      <div class="workspace-grid">
        <aside class="rail project-rail" aria-labelledby="project-rail-title">
          <div class="panel-head">
            <div>
              <p class="eyebrow">PROJECTS</p>
              <h2 id="project-rail-title">项目库</h2>
            </div>
            <FolderOpen :size="22" class="heading-icon" aria-hidden="true" />
          </div>

          <form class="project-form" @submit.prevent="saveActiveProject">
            <label>
              项目名称
              <input v-model="projectDraft.name" type="text" maxlength="120" autocomplete="off" />
            </label>
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
            <div class="field-grid two time-range-fields">
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
                <FolderPlus :size="18" aria-hidden="true" />
                {{ projectActionLabel }}
              </button>
              <button class="secondary-button" type="button" :disabled="Boolean(projectFormError)" @click="createProject">
                新建项目
              </button>
            </div>
          </form>

          <div class="project-list">
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
              <span>游标 {{ project.cursorIndex + 1 }} · {{ speedToText(project.speed) }}</span>
            </button>
          </div>
        </aside>

        <section class="stage" aria-label="主图表区域">
          <MarketChart
            :snapshot="replaySnapshot"
            :loading="replayLoading"
            :playback-index="chartPlaybackIndex"
            :playing="workspaceMode === 'replay' && playbackPlaying"
            :project-name="currentProjectLabel"
          />

          <div v-if="workspaceMode === 'replay'" class="transport-bar" aria-label="K 线播放控制">
            <div class="transport-group">
              <button class="icon-button" type="button" aria-label="跳到开头" title="跳到开头" @click="seekToStart">
                <ChevronsLeft :size="18" aria-hidden="true" />
              </button>
              <button class="icon-button" type="button" aria-label="上一根 K 线" title="上一根" @click="stepReplay(-1)">
                <ChevronLeft :size="18" aria-hidden="true" />
              </button>
              <button class="primary-button transport-play" type="button" @click="togglePlayback">
                <Pause v-if="playbackPlaying" :size="18" aria-hidden="true" />
                <Play v-else :size="18" aria-hidden="true" />
                {{ playbackPlaying ? '暂停播放' : '开始播放' }}
              </button>
              <button class="icon-button" type="button" aria-label="下一根 K 线" title="下一根" @click="stepReplay(1)">
                <ChevronRight :size="18" aria-hidden="true" />
              </button>
              <button class="icon-button" type="button" aria-label="跳到末尾" title="跳到末尾" @click="seekToEnd">
                <ChevronsRight :size="18" aria-hidden="true" />
              </button>
            </div>

            <div class="transport-meta">
              <label class="speed-control">
                <span><SlidersHorizontal :size="16" aria-hidden="true" /> 播放速度</span>
                <input
                  :value="activeProject?.speed ?? 1"
                  type="range"
                  min="0.25"
                  max="8"
                  step="0.25"
                  :disabled="!activeProject"
                  @input="changePlaybackSpeed"
                />
                <output>{{ currentSpeedLabel }}</output>
              </label>

              <label class="progress-control">
                <span>
                  <TimerReset :size="16" aria-hidden="true" />
                  进度
                </span>
                <input
                  :value="activePlaybackIndex"
                  type="range"
                  min="0"
                  :max="Math.max(totalReplayCandles - 1, 0)"
                  :disabled="!totalReplayCandles"
                  @input="seekReplay"
                />
                <small>{{ currentProgressLabel }} · {{ currentTimeLabel }}</small>
              </label>
            </div>
          </div>

          <div v-else class="backtest-strip">
            <div>
              <p class="eyebrow">BACKTEST RANGE</p>
              <strong>{{ currentProjectLabel }}</strong>
            </div>
            <p>当前项目的 UTC 时间范围会作为回测输入，右侧面板可直接提交策略。</p>
          </div>
        </section>

        <aside class="rail inspector-rail" aria-label="检查器">
          <template v-if="workspaceMode === 'replay'">
            <section class="panel inspector-panel">
              <div class="panel-head">
                <div>
                  <p class="eyebrow">SESSION</p>
                  <h2>播放检查器</h2>
                </div>
                <Gauge :size="22" class="heading-icon" aria-hidden="true" />
              </div>

              <dl class="metric-grid compact">
                <div>
                  <dt>状态</dt>
                  <dd>{{ replayStatusLabel }}</dd>
                </div>
                <div>
                  <dt>速度</dt>
                  <dd>{{ currentSpeedLabel }}</dd>
                </div>
                <div>
                  <dt>进度</dt>
                  <dd>{{ currentProgressLabel }}</dd>
                </div>
                <div>
                  <dt>来源</dt>
                  <dd>{{ replaySnapshot?.source ?? '—' }}</dd>
                </div>
              </dl>

              <div class="progress-meter" aria-hidden="true">
                <span :style="{ width: `${currentProgressPercent}%` }" />
              </div>

              <div v-if="currentCandle" class="candle-card">
                <div class="candle-card-head">
                  <Clock3 :size="18" aria-hidden="true" />
                  <span>{{ currentTimeLabel }}</span>
                </div>
                <dl>
                  <div><dt>开</dt><dd>{{ formatNumber(currentCandle.open) }}</dd></div>
                  <div><dt>高</dt><dd>{{ formatNumber(currentCandle.high) }}</dd></div>
                  <div><dt>低</dt><dd>{{ formatNumber(currentCandle.low) }}</dd></div>
                  <div><dt>收</dt><dd>{{ formatNumber(currentCandle.close) }}</dd></div>
                  <div><dt>量</dt><dd>{{ formatNumber(currentCandle.volume) }}</dd></div>
                </dl>
              </div>

              <div class="mini-actions">
                <button class="secondary-button" type="button" @click="saveActiveProject">
                  <RefreshCw :size="16" aria-hidden="true" />
                  保存当前项目
                </button>
              </div>
            </section>
          </template>

          <template v-else>
            <BacktestPanel
              :timeframe="activeProjectTimeframe"
              :from="activeProjectFrom"
              :to="activeProjectTo"
              :running="backtestRunning"
              :record="lastRecord"
              @submit="runBacktest"
            />
            <HistoryPanel :records="history" :loading="historyLoading" />
          </template>
        </aside>
      </div>
    </main>
  </div>
</template>
