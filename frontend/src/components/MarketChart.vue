<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, ref, watch } from 'vue'
import {
  CandlestickSeries,
  ColorType,
  createChart,
  LineSeries,
  LineStyle,
  type IChartApi,
  type ISeriesApi,
  type UTCTimestamp,
} from 'lightweight-charts'
import { RotateCcw, ZoomIn, ZoomOut } from '@lucide/vue'
import type { MarketSnapshot } from '../types'
import { formatNumber, formatUtc } from '../format'

const props = withDefaults(defineProps<{
  snapshot: MarketSnapshot | null
  loading: boolean
  playbackIndex?: number
  playing?: boolean
  projectName?: string
}>(), {
  playbackIndex: undefined,
  playing: false,
  projectName: '',
})

const priceContainer = ref<HTMLElement | null>(null)
const stochContainer = ref<HTMLElement | null>(null)
const newestFirst = ref(true)
let priceChart: IChartApi | null = null
let stochChart: IChartApi | null = null
let candleSeries: ISeriesApi<'Candlestick'> | null = null
let stochasticSeries: Array<{ k: ISeriesApi<'Line'>; d: ISeriesApi<'Line'> }> = []
let resizeObserver: ResizeObserver | null = null
let syncing = false
let needsFit = true

const totalCandles = computed(() => props.snapshot?.candles.length ?? 0)

const visibleCount = computed(() => {
  const total = totalCandles.value
  if (!total) return 0
  const cursor = props.playbackIndex ?? total - 1
  return Math.min(Math.max(cursor + 1, 1), total)
})

const visibleCandles = computed(() => props.snapshot?.candles.slice(0, visibleCount.value) ?? [])
const visibleStochastic = computed(() => props.snapshot?.stochastic.slice(0, visibleCount.value) ?? [])
const currentCandle = computed(() => visibleCandles.value.at(-1) ?? null)

const tableCandles = computed(() => {
  const candles = visibleCandles.value.slice(-12)
  return newestFirst.value ? [...candles].reverse() : candles
})

const summary = computed(() => {
  const candles = visibleCandles.value
  if (!candles.length) return '当前范围没有 K 线数据。'
  const first = candles[0]
  const last = currentCandle.value ?? candles[candles.length - 1]
  const change = ((last.close / first.open) - 1) * 100
  const progress = totalCandles.value ? `${candles.length}/${totalCandles.value}` : `${candles.length}`
  return `${props.projectName ? `${props.projectName} · ` : ''}${progress} 根 ${props.snapshot?.timeframe} K 线，从 ${formatUtc(first.timestamp)} 至 ${formatUtc(last.timestamp)} UTC，区间涨跌 ${change >= 0 ? '上涨' : '下跌'} ${Math.abs(change).toFixed(2)}%。`
})

function chartOptions(height: number) {
  return {
    height,
    layout: {
      background: { type: ColorType.Solid, color: '#08101d' },
      textColor: '#a6b3c7',
      fontFamily: '"Fira Code", "IBM Plex Mono", "SFMono-Regular", Consolas, monospace',
    },
    grid: {
      vertLines: { color: '#162033' },
      horzLines: { color: '#162033' },
    },
    rightPriceScale: { borderColor: '#324154' },
    timeScale: { borderColor: '#324154', timeVisible: true, secondsVisible: false },
    crosshair: { vertLine: { color: '#64748b' }, horzLine: { color: '#64748b' } },
  }
}

function disposeCharts() {
  resizeObserver?.disconnect()
  resizeObserver = null
  priceChart?.remove()
  stochChart?.remove()
  priceChart = null
  stochChart = null
  candleSeries = null
  stochasticSeries = []
  syncing = false
  needsFit = true
}

function setSeriesData() {
  if (!priceChart || !stochChart || !candleSeries) return

  const candles = visibleCandles.value
  const stochastic = visibleStochastic.value

  candleSeries.setData(candles.map((candle) => ({
    time: candle.timestamp as UTCTimestamp,
    open: candle.open,
    high: candle.high,
    low: candle.low,
    close: candle.close,
  })))

  for (let group = 0; group < 3; group += 1) {
    stochasticSeries[group]?.k.setData(stochastic.flatMap((point) =>
      point.k[group] == null ? [] : [{ time: point.timestamp as UTCTimestamp, value: point.k[group] }],
    ))
    stochasticSeries[group]?.d.setData(stochastic.flatMap((point) =>
      point.d[group] == null ? [] : [{ time: point.timestamp as UTCTimestamp, value: point.d[group] }],
    ))
  }

  if (needsFit) {
    priceChart.timeScale().fitContent()
    stochChart.timeScale().fitContent()
    needsFit = false
  } else {
    priceChart.timeScale().scrollToRealTime()
    stochChart.timeScale().scrollToRealTime()
  }
}

function buildCharts() {
  if (!priceContainer.value || !stochContainer.value || !props.snapshot?.candles.length) return
  disposeCharts()
  priceChart = createChart(priceContainer.value, chartOptions(420))
  stochChart = createChart(stochContainer.value, chartOptions(180))
  candleSeries = priceChart.addSeries(CandlestickSeries, {
    upColor: '#2dd4bf',
    downColor: '#fb7185',
    borderUpColor: '#5eead4',
    borderDownColor: '#fda4af',
    wickUpColor: '#5eead4',
    wickDownColor: '#fda4af',
  })
  const palette = [
    ['#38bdf8', '#f59e0b'],
    ['#22c55e', '#fb7185'],
    ['#a3e635', '#f97316'],
  ]
  stochasticSeries = palette.map(([k, d]) => ({
    k: stochChart!.addSeries(LineSeries, { color: k, lineWidth: 2, priceLineVisible: false }),
    d: stochChart!.addSeries(LineSeries, { color: d, lineWidth: 1, lineStyle: LineStyle.Dashed, priceLineVisible: false }),
  }))
  for (const value of [20, 50, 80]) {
    stochasticSeries[0]?.k.createPriceLine({
      price: value,
      color: value === 50 ? '#475569' : '#64748b',
      lineWidth: 1,
      lineStyle: LineStyle.Dotted,
      axisLabelVisible: true,
      title: String(value),
    })
  }

  priceChart.timeScale().subscribeVisibleLogicalRangeChange((range) => {
    if (!range || syncing) return
    syncing = true
    stochChart?.timeScale().setVisibleLogicalRange(range)
    syncing = false
  })
  stochChart.timeScale().subscribeVisibleLogicalRangeChange((range) => {
    if (!range || syncing) return
    syncing = true
    priceChart?.timeScale().setVisibleLogicalRange(range)
    syncing = false
  })
  resizeObserver?.disconnect()
  if (typeof ResizeObserver !== 'undefined') {
    resizeObserver = new ResizeObserver(() => {
      if (!priceContainer.value || !stochContainer.value) return
      priceChart?.applyOptions({ width: priceContainer.value.clientWidth })
      stochChart?.applyOptions({ width: stochContainer.value.clientWidth })
    })
    resizeObserver.observe(priceContainer.value)
    resizeObserver.observe(stochContainer.value)
  }

  needsFit = true
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
  needsFit = true
  setSeriesData()
}

watch(() => props.snapshot, async () => {
  needsFit = true
  await nextTick()
  if (props.snapshot?.candles.length) {
    buildCharts()
  } else {
    disposeCharts()
  }
}, { immediate: true })

watch([() => props.playbackIndex, () => props.playing], async () => {
  await nextTick()
  setSeriesData()
})

onBeforeUnmount(() => {
  disposeCharts()
})
</script>

<template>
  <section class="chart-card" aria-labelledby="market-chart-title">
    <div class="card-heading chart-heading">
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
    <template v-else>
      <div ref="priceContainer" class="price-chart" aria-hidden="true" />
      <div class="indicator-legend" aria-label="Stoch 指标图例">
        <span><i class="legend-dot k1" />K1 / D1</span>
        <span><i class="legend-dot k2" />K2 / D2</span>
        <span><i class="legend-dot k3" />K3 / D3</span>
      </div>
      <div ref="stochContainer" class="stoch-chart" aria-hidden="true" />
    </template>

    <details class="data-table-panel">
      <summary>查看当前播放段最近 12 根 OHLC 数据</summary>
      <div class="table-toolbar">
        <p>{{ summary }}</p>
        <button type="button" class="text-button" :aria-pressed="newestFirst" @click="newestFirst = !newestFirst">
          {{ newestFirst ? '最新优先' : '最早优先' }}
        </button>
      </div>
      <div class="table-scroll">
        <table>
          <thead>
            <tr><th>UTC 时间</th><th>方向</th><th>开</th><th>高</th><th>低</th><th>收</th></tr>
          </thead>
          <tbody>
            <tr v-for="candle in tableCandles" :key="candle.timestamp">
              <td>{{ formatUtc(candle.timestamp) }}</td>
              <td>{{ candle.close >= candle.open ? '上涨' : '下跌' }}</td>
              <td>{{ formatNumber(candle.open) }}</td>
              <td>{{ formatNumber(candle.high) }}</td>
              <td>{{ formatNumber(candle.low) }}</td>
              <td>{{ formatNumber(candle.close) }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </details>
  </section>
</template>
