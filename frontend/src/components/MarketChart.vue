<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import {
  CandlestickSeries,
  ColorType,
  createChart,
  createSeriesMarkers,
  type IChartApi,
  type ISeriesMarkersPluginApi,
  type ISeriesApi,
  type SeriesMarker,
  type Time,
  type UTCTimestamp,
} from 'lightweight-charts'
import { RotateCcw, Spline, Trash2, X, ZoomIn, ZoomOut } from '@lucide/vue'
import { createTrendLine, type ManualTrade, type TrendLine, type TrendLinePoint } from '../replay'
import type { MarketSnapshot } from '../types'
import { formatUtc } from '../format'

const props = withDefaults(defineProps<{
  snapshot: MarketSnapshot | null
  loading: boolean
  playbackIndex?: number
  playing?: boolean
  projectName?: string
  showHeader?: boolean
  manualTrades?: ManualTrade[]
  trendLines?: TrendLine[]
}>(), {
  playbackIndex: undefined,
  playing: false,
  projectName: '',
  showHeader: true,
  manualTrades: () => [],
  trendLines: () => [],
})

const emit = defineEmits<{
  (event: 'trendline-created', line: TrendLine): void
  (event: 'trendlines-cleared'): void
}>()

const priceContainer = ref<HTMLElement | null>(null)
let priceChart: IChartApi | null = null
let candleSeries: ISeriesApi<'Candlestick'> | null = null
let tradeMarkers: ISeriesMarkersPluginApi<Time> | null = null
let resizeObserver: ResizeObserver | null = null
let visibleRangeHandler: (() => void) | null = null
let feedbackTimer: number | null = null
let activePointerId: number | null = null
let pointerMoved = false
let pointerStartedWithAnchor = false
let pointerStartClientX = 0
let pointerStartClientY = 0

const chartDimensions = ref({ width: 0, height: 0 })
const renderRevision = ref(0)
const drawingMode = ref(false)
const drawingAnchor = ref<TrendLinePoint | null>(null)
const previewPoint = ref<TrendLinePoint | null>(null)
const lineFeedback = ref('')

const chartTheme = {
  background: '#000000',
  text: '#dbe4ee',
  border: '#334155',
  crosshair: '#cbd5e1',
  candleUp: 'transparent',
  candleUpBorder: '#ffffff',
  candleDown: '#ffffff',
  candleDownBorder: '#ffffff',
  markerBuy: '#ffffff',
  markerSell: '#dbe4ee',
  trendline: '#facc15',
}

const totalCandles = computed(() => props.snapshot?.candles.length ?? 0)

const visibleCount = computed(() => {
  const total = totalCandles.value
  if (!total) return 0
  const cursor = props.playbackIndex ?? total - 1
  return Math.min(Math.max(cursor + 1, 1), total)
})

const visibleCandles = computed(() => props.snapshot?.candles.slice(0, visibleCount.value) ?? [])
const currentCandle = computed(() => visibleCandles.value.at(-1) ?? null)
const visibleTradeMarkers = computed<SeriesMarker<Time>[]>(() =>
  props.manualTrades
    .filter((trade) => trade.candleIndex < visibleCount.value)
    .map((trade) => ({
      id: trade.id,
      time: trade.timestamp as UTCTimestamp,
      position: trade.side === 'buy' ? 'belowBar' : 'aboveBar',
      shape: trade.side === 'buy' ? 'arrowUp' : 'arrowDown',
      color: trade.side === 'buy' ? chartTheme.markerBuy : chartTheme.markerSell,
      text: trade.side === 'buy' ? 'BUY' : 'SELL',
      size: 1.25,
    })),
)

function toScreenPoint(point: TrendLinePoint) {
  if (!priceChart || !candleSeries) return null
  const x = priceChart.timeScale().timeToCoordinate(point.timestamp as UTCTimestamp)
  const y = candleSeries.priceToCoordinate(point.price)
  if (x === null || y === null) return null
  return { x: Number(x), y: Number(y) }
}

const renderedTrendLines = computed(() => {
  renderRevision.value
  return props.trendLines
    .map((line) => {
      const start = toScreenPoint(line.start)
      const end = toScreenPoint(line.end)
      return start && end ? { ...line, start, end } : null
    })
    .filter((line): line is NonNullable<typeof line> => Boolean(line))
})

const selectedAnchor = computed(() => {
  renderRevision.value
  return drawingAnchor.value ? toScreenPoint(drawingAnchor.value) : null
})

const previewLine = computed(() => {
  renderRevision.value
  if (!drawingAnchor.value || !previewPoint.value) return null
  const start = toScreenPoint(drawingAnchor.value)
  const end = toScreenPoint(previewPoint.value)
  return start && end ? { start, end } : null
})

const drawingHint = computed(() => {
  if (lineFeedback.value) return lineFeedback.value
  if (drawingAnchor.value) return '已选择第一个锚点，点击或拖到另一根 K 线完成趋势线。'
  return '点击两根 K 线设置锚点；按 Esc 取消。'
})

const chartViewBox = computed(() => `0 0 ${chartDimensions.value.width} ${chartDimensions.value.height}`)

const summary = computed(() => {
  const candles = visibleCandles.value
  if (!candles.length) return '当前范围没有 K 线数据。'
  const first = candles[0]
  const last = currentCandle.value ?? candles[candles.length - 1]
  const change = ((last.close / first.open) - 1) * 100
  const progress = totalCandles.value ? `${candles.length}/${totalCandles.value}` : `${candles.length}`
  const trendlineSummary = props.trendLines.length ? `，已绘制 ${props.trendLines.length} 条趋势线` : ''
  return `${props.projectName ? `${props.projectName} · ` : ''}${progress} 根 ${props.snapshot?.timeframe} K 线，从 ${formatUtc(first.timestamp)} 至 ${formatUtc(last.timestamp)} UTC，区间涨跌 ${change >= 0 ? '上涨' : '下跌'} ${Math.abs(change).toFixed(2)}%${trendlineSummary}。`
})

function containerSize() {
  const container = priceContainer.value
  return {
    width: Math.max(container?.clientWidth ?? 0, 320),
    height: Math.max(container?.clientHeight ?? 0, 360),
  }
}

function bumpRender() {
  renderRevision.value += 1
}

function chartOptions() {
  const size = containerSize()
  return {
    width: size.width,
    height: size.height,
    layout: {
      background: { type: ColorType.Solid, color: chartTheme.background },
      textColor: chartTheme.text,
      fontFamily: '"Fira Code", "IBM Plex Mono", "SFMono-Regular", Consolas, monospace',
    },
    grid: {
      vertLines: { color: 'rgba(0, 0, 0, 0)', visible: false },
      horzLines: { color: 'rgba(0, 0, 0, 0)', visible: false },
    },
    rightPriceScale: {
      borderColor: chartTheme.border,
      scaleMargins: {
        top: 0.08,
        bottom: 0.1,
      },
    },
    timeScale: {
      borderColor: chartTheme.border,
      timeVisible: true,
      secondsVisible: false,
      rightOffset: 8,
      barSpacing: 12,
    },
    crosshair: {
      vertLine: { color: chartTheme.crosshair },
      horzLine: { color: chartTheme.crosshair },
    },
  }
}

function disposeCharts() {
  resizeObserver?.disconnect()
  resizeObserver = null
  if (priceChart && visibleRangeHandler) {
    priceChart.timeScale().unsubscribeVisibleLogicalRangeChange(visibleRangeHandler)
  }
  visibleRangeHandler = null
  priceChart?.remove()
  priceChart = null
  candleSeries = null
  tradeMarkers = null
  chartDimensions.value = { width: 0, height: 0 }
  cancelPendingLine()
}

function playbackWindowBars() {
  const { width } = containerSize()
  return Math.min(Math.max(Math.floor(width / 12), 80), 180)
}

function setPlaybackRange() {
  const scale = priceChart?.timeScale()
  const count = visibleCandles.value.length
  if (!scale || !count) return

  const bars = playbackWindowBars()
  const rightOffset = Math.max(6, Math.floor(bars * 0.08))
  const right = count - 1 + rightOffset
  scale.setVisibleLogicalRange({
    from: right - bars,
    to: right,
  })
}

function setSeriesData() {
  if (!priceChart || !candleSeries) return

  candleSeries.setData(visibleCandles.value.map((candle) => ({
    time: candle.timestamp as UTCTimestamp,
    open: candle.open,
    high: candle.high,
    low: candle.low,
    close: candle.close,
  })))

  tradeMarkers?.setMarkers(visibleTradeMarkers.value)
  setPlaybackRange()
  bumpRender()
}

function resizeChart() {
  if (!priceChart) return
  const size = containerSize()
  chartDimensions.value = size
  priceChart.applyOptions(size)
  setPlaybackRange()
  bumpRender()
}

function buildCharts() {
  if (!priceContainer.value || !props.snapshot?.candles.length) return
  disposeCharts()
  priceChart = createChart(priceContainer.value, chartOptions())
  candleSeries = priceChart.addSeries(CandlestickSeries, {
    upColor: chartTheme.candleUp,
    downColor: chartTheme.candleDown,
    borderUpColor: chartTheme.candleUpBorder,
    borderDownColor: chartTheme.candleDownBorder,
    wickUpColor: chartTheme.candleUpBorder,
    wickDownColor: chartTheme.candleDownBorder,
    priceLineVisible: false,
  })
  tradeMarkers = createSeriesMarkers(candleSeries, [], { autoScale: true, zOrder: 'top' })
  visibleRangeHandler = () => bumpRender()
  priceChart.timeScale().subscribeVisibleLogicalRangeChange(visibleRangeHandler)

  resizeObserver?.disconnect()
  if (typeof ResizeObserver !== 'undefined') {
    resizeObserver = new ResizeObserver(() => {
      resizeChart()
    })
    resizeObserver.observe(priceContainer.value)
  }

  resizeChart()
  setSeriesData()
}

function zoom(factor: number) {
  const scale = priceChart?.timeScale()
  const range = scale?.getVisibleLogicalRange()
  if (!scale || !range) return
  const center = (range.from + range.to) / 2
  const half = ((range.to - range.from) / 2) * factor
  scale.setVisibleLogicalRange({ from: center - half, to: center + half })
  bumpRender()
}

function resetView() {
  resizeChart()
  setPlaybackRange()
  bumpRender()
}

function clearFeedbackTimer() {
  if (feedbackTimer !== null) {
    window.clearTimeout(feedbackTimer)
    feedbackTimer = null
  }
}

function setLineFeedback(message: string) {
  clearFeedbackTimer()
  lineFeedback.value = message
  if (!message) return
  feedbackTimer = window.setTimeout(() => {
    lineFeedback.value = ''
    feedbackTimer = null
  }, 2200)
}

function cancelPendingLine() {
  drawingAnchor.value = null
  previewPoint.value = null
  activePointerId = null
  pointerMoved = false
  pointerStartedWithAnchor = false
  pointerStartClientX = 0
  pointerStartClientY = 0
}

function toggleDrawingMode() {
  drawingMode.value = !drawingMode.value
  cancelPendingLine()
  setLineFeedback('')
}

function cancelDrawing() {
  if (drawingAnchor.value) {
    cancelPendingLine()
    setLineFeedback('待完成的趋势线已取消。')
    return
  }
  drawingMode.value = false
  setLineFeedback('')
}

function completeTrendLine(end: TrendLinePoint) {
  const start = drawingAnchor.value
  if (!start) return
  if (start.timestamp === end.timestamp) {
    setLineFeedback('请选择不同的 K 线作为第二个锚点。')
    previewPoint.value = null
    return
  }
  const line = createTrendLine({ start, end })
  if (!line) return
  emit('trendline-created', line)
  cancelPendingLine()
  setLineFeedback('趋势线已添加，可继续绘制。')
}

function clearTrendLines() {
  if (!props.trendLines.length) return
  if (typeof window !== 'undefined' && !window.confirm('确定清除当前项目的全部趋势线吗？')) return
  emit('trendlines-cleared')
  drawingMode.value = false
  cancelPendingLine()
  setLineFeedback('趋势线已清除。')
}

function pointFromPointer(event: PointerEvent): TrendLinePoint | null {
  if (!priceChart || !candleSeries || !priceContainer.value) return null
  const rect = priceContainer.value.getBoundingClientRect()
  const x = event.clientX - rect.left
  const y = event.clientY - rect.top
  if (x < 0 || x > rect.width || y < 0 || y > rect.height) return null

  const timeScale = priceChart.timeScale()
  if (x > timeScale.width()) return null
  const timeScaleHeight = timeScale.height()
  if (y > rect.height - timeScaleHeight) return null

  const logical = timeScale.coordinateToLogical(x)
  if (logical === null) return null
  const index = Math.round(Number(logical))
  const candle = visibleCandles.value[index]
  if (!candle) return null

  const price = candleSeries.coordinateToPrice(y)
  if (price === null || !Number.isFinite(Number(price))) return null
  return {
    timestamp: candle.timestamp,
    price: Number(price),
  }
}

function handleDrawingPointerDown(event: PointerEvent) {
  if (!drawingMode.value || event.button !== 0) return
  const point = pointFromPointer(event)
  if (!point) return
  event.preventDefault()
  event.stopPropagation()
  activePointerId = event.pointerId
  pointerMoved = false
  pointerStartedWithAnchor = Boolean(drawingAnchor.value)
  pointerStartClientX = event.clientX
  pointerStartClientY = event.clientY
  if (!drawingAnchor.value) {
    drawingAnchor.value = point
  } else {
    previewPoint.value = point
  }
  const target = event.currentTarget as SVGElement | null
  target?.setPointerCapture?.(event.pointerId)
}

function handleDrawingPointerMove(event: PointerEvent) {
  if (!drawingMode.value) return
  if (activePointerId !== null && event.pointerId !== activePointerId) return
  if (
    activePointerId !== null &&
    (event.clientX - pointerStartClientX) ** 2 + (event.clientY - pointerStartClientY) ** 2 > 36
  ) {
    pointerMoved = true
  }
  if (!drawingAnchor.value) return
  previewPoint.value = pointFromPointer(event)
}

function releasePointer(event: PointerEvent) {
  const target = event.currentTarget as SVGElement | null
  if (target?.hasPointerCapture?.(event.pointerId)) {
    target.releasePointerCapture(event.pointerId)
  }
  activePointerId = null
  pointerMoved = false
  pointerStartedWithAnchor = false
}

function handleDrawingPointerUp(event: PointerEvent) {
  if (!drawingMode.value || event.button !== 0) return
  if (activePointerId !== null && event.pointerId !== activePointerId) return
  const point = pointFromPointer(event)
  const shouldComplete = Boolean(drawingAnchor.value && (pointerStartedWithAnchor || pointerMoved))
  if (shouldComplete && point) {
    completeTrendLine(point)
  } else if (drawingAnchor.value) {
    previewPoint.value = null
  }
  releasePointer(event)
}

function handleDrawingPointerCancel(event: PointerEvent) {
  if (activePointerId !== null && event.pointerId !== activePointerId) return
  previewPoint.value = null
  releasePointer(event)
}

function handleDrawingPointerLeave(event: PointerEvent) {
  if (activePointerId === null || event.pointerId !== activePointerId) {
    previewPoint.value = null
  }
}

function handleKeyDown(event: KeyboardEvent) {
  if (event.key !== 'Escape' || !drawingMode.value) return
  event.preventDefault()
  cancelDrawing()
}

watch(() => props.snapshot, async () => {
  await nextTick()
  if (props.snapshot?.candles.length) {
    buildCharts()
  } else {
    disposeCharts()
  }
}, { immediate: true })

watch([() => props.playbackIndex, () => props.playing, () => props.manualTrades], async () => {
  await nextTick()
  resizeChart()
  setSeriesData()
})

watch(() => props.trendLines, () => {
  bumpRender()
})

onMounted(() => {
  window.addEventListener('keydown', handleKeyDown)
})

onBeforeUnmount(() => {
  window.removeEventListener('keydown', handleKeyDown)
  clearFeedbackTimer()
  disposeCharts()
})
</script>

<template>
  <section class="chart-card" :class="{ 'chart-card-bare': !showHeader }" aria-labelledby="market-chart-title">
    <div v-if="showHeader" class="card-heading chart-heading">
      <div>
        <p class="eyebrow">PLAYBACK VIEW</p>
        <h2 id="market-chart-title">BTCUSD 复盘图</h2>
        <p class="chart-note">{{ summary }}</p>
      </div>
      <div class="chart-status">
        <span class="status-chip" :data-status="playing ? 'running' : 'paused'">{{ playing ? '播放中' : '已暂停' }}</span>
        <span class="status-chip muted">{{ visibleCount }}/{{ totalCandles }}</span>
      </div>
      <div class="chart-actions" aria-label="图表缩放控制">
        <button class="icon-button" type="button" aria-label="放大图表" title="放大" @click="zoom(0.75)">
          <ZoomIn :size="18" aria-hidden="true" />
        </button>
        <button class="icon-button" type="button" aria-label="缩小图表" title="缩小" @click="zoom(1.3)">
          <ZoomOut :size="18" aria-hidden="true" />
        </button>
        <button class="icon-button" type="button" aria-label="重置图表范围" title="重置" @click="resetView">
          <RotateCcw :size="18" aria-hidden="true" />
        </button>
      </div>
    </div>

    <p class="sr-only" role="status">{{ summary }}</p>
    <div v-if="loading" class="chart-skeleton" aria-label="正在加载行情" />
    <div v-else-if="!snapshot?.candles.length" class="empty-state">
      <p>当前时间范围没有行情数据。</p>
      <span>请调整 UTC 时间范围后重新加载。</span>
    </div>
    <div v-else class="chart-stage">
      <div ref="priceContainer" class="price-chart" aria-hidden="true" />

      <svg
        class="trendline-layer"
        :class="{ 'is-drawing': drawingMode }"
        :viewBox="chartViewBox"
        preserveAspectRatio="none"
        aria-hidden="true"
        @pointerdown="handleDrawingPointerDown"
        @pointermove="handleDrawingPointerMove"
        @pointerup="handleDrawingPointerUp"
        @pointercancel="handleDrawingPointerCancel"
        @pointerleave="handleDrawingPointerLeave"
      >
        <g v-for="line in renderedTrendLines" :key="line.id" class="trendline-render">
          <line
            class="trendline-line"
            :x1="line.start.x"
            :y1="line.start.y"
            :x2="line.end.x"
            :y2="line.end.y"
          />
          <circle class="trendline-endpoint" :cx="line.start.x" :cy="line.start.y" r="4" />
          <circle class="trendline-endpoint" :cx="line.end.x" :cy="line.end.y" r="4" />
        </g>
        <line
          v-if="previewLine"
          class="trendline-line trendline-preview"
          :x1="previewLine.start.x"
          :y1="previewLine.start.y"
          :x2="previewLine.end.x"
          :y2="previewLine.end.y"
        />
        <circle
          v-if="selectedAnchor"
          class="trendline-endpoint trendline-anchor"
          :cx="selectedAnchor.x"
          :cy="selectedAnchor.y"
          r="5"
        />
      </svg>

      <div class="chart-toolbox" aria-label="趋势线工具">
        <button
          class="chart-tool-button"
          :class="{ active: drawingMode }"
          type="button"
          :aria-pressed="drawingMode"
          :disabled="loading || !totalCandles"
          :title="drawingMode ? '退出趋势线绘制' : '绘制趋势线'"
          @click="toggleDrawingMode"
        >
          <Spline :size="16" aria-hidden="true" />
          <span>{{ drawingMode ? '退出绘制' : '趋势线' }}</span>
          <small v-if="trendLines.length">{{ trendLines.length }}</small>
        </button>
        <button
          v-if="trendLines.length"
          class="chart-tool-button chart-tool-clear"
          type="button"
          aria-label="清除全部趋势线"
          title="清除全部趋势线"
          @click="clearTrendLines"
        >
          <Trash2 :size="16" aria-hidden="true" />
          <span>清除</span>
        </button>
        <button
          v-if="drawingAnchor"
          class="chart-tool-button chart-tool-cancel"
          type="button"
          aria-label="取消当前趋势线"
          title="取消当前趋势线"
          @click="cancelPendingLine"
        >
          <X :size="16" aria-hidden="true" />
          <span>取消</span>
        </button>
      </div>

      <p v-if="drawingMode || lineFeedback" class="trendline-hint" role="status" aria-live="polite">
        {{ drawingHint }}
      </p>
    </div>
  </section>
</template>
