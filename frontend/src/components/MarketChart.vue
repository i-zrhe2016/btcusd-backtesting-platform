<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue'
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
import { RotateCcw, ZoomIn, ZoomOut } from '@lucide/vue'
import type { ManualTrade } from '../replay'
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
}>(), {
  playbackIndex: undefined,
  playing: false,
  projectName: '',
  showHeader: true,
  manualTrades: () => [],
})

const priceContainer = ref<HTMLElement | null>(null)
let priceChart: IChartApi | null = null
let candleSeries: ISeriesApi<'Candlestick'> | null = null
let tradeMarkers: ISeriesMarkersPluginApi<Time> | null = null
let resizeObserver: ResizeObserver | null = null

const chartTheme = {
  background: '#000000',
  text: '#dbe4ee',
  border: '#334155',
  crosshair: '#cbd5e1',
  candleUp: '#22c55e',
  candleUpBorder: '#4ade80',
  candleDown: '#ef4444',
  candleDownBorder: '#f87171',
  markerBuy: '#22c55e',
  markerSell: '#ef4444',
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

const summary = computed(() => {
  const candles = visibleCandles.value
  if (!candles.length) return '当前范围没有 K 线数据。'
  const first = candles[0]
  const last = currentCandle.value ?? candles[candles.length - 1]
  const change = ((last.close / first.open) - 1) * 100
  const progress = totalCandles.value ? `${candles.length}/${totalCandles.value}` : `${candles.length}`
  return `${props.projectName ? `${props.projectName} · ` : ''}${progress} 根 ${props.snapshot?.timeframe} K 线，从 ${formatUtc(first.timestamp)} 至 ${formatUtc(last.timestamp)} UTC，区间涨跌 ${change >= 0 ? '上涨' : '下跌'} ${Math.abs(change).toFixed(2)}%。`
})

function containerSize() {
  const container = priceContainer.value
  return {
    width: Math.max(container?.clientWidth ?? 0, 320),
    height: Math.max(container?.clientHeight ?? 0, 360),
  }
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
  priceChart?.remove()
  priceChart = null
  candleSeries = null
  tradeMarkers = null
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
}

function resizeChart() {
  if (!priceChart) return
  priceChart.applyOptions(containerSize())
  setPlaybackRange()
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
}

function resetView() {
  resizeChart()
  setPlaybackRange()
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

onBeforeUnmount(() => {
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
    <div v-else ref="priceContainer" class="price-chart" aria-hidden="true" />
  </section>
</template>
