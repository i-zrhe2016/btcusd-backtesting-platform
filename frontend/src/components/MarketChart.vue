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

const props = defineProps<{
  snapshot: MarketSnapshot | null
  loading: boolean
}>()

const priceContainer = ref<HTMLElement | null>(null)
const stochContainer = ref<HTMLElement | null>(null)
const newestFirst = ref(true)
let priceChart: IChartApi | null = null
let stochChart: IChartApi | null = null
let candleSeries: ISeriesApi<'Candlestick'> | null = null
let resizeObserver: ResizeObserver | null = null
let syncing = false

const tableCandles = computed(() => {
  const candles = props.snapshot?.candles.slice(-12) ?? []
  return newestFirst.value ? candles.reverse() : candles
})

const summary = computed(() => {
  const candles = props.snapshot?.candles ?? []
  if (!candles.length) return '当前范围没有 K 线数据。'
  const first = candles[0]
  const last = candles[candles.length - 1]
  const change = ((last.close / first.open) - 1) * 100
  return `${candles.length} 根 ${props.snapshot?.timeframe} K 线，从 ${formatUtc(first.timestamp)} 至 ${formatUtc(last.timestamp)} UTC，区间涨跌 ${change >= 0 ? '上涨' : '下跌'} ${Math.abs(change).toFixed(2)}%。`
})

function chartOptions(height: number) {
  return {
    height,
    layout: {
      background: { type: ColorType.Solid, color: '#070b16' },
      textColor: '#94a3b8',
      fontFamily: '"IBM Plex Mono", "SFMono-Regular", Consolas, monospace',
    },
    grid: {
      vertLines: { color: '#172033' },
      horzLines: { color: '#172033' },
    },
    rightPriceScale: { borderColor: '#334155' },
    timeScale: { borderColor: '#334155', timeVisible: true, secondsVisible: false },
    crosshair: { vertLine: { color: '#64748b' }, horzLine: { color: '#64748b' } },
  }
}

function buildCharts() {
  if (!priceContainer.value || !stochContainer.value) return
  priceChart?.remove()
  stochChart?.remove()
  priceChart = createChart(priceContainer.value, chartOptions(410))
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
    ['#38bdf8', '#facc15'],
    ['#a78bfa', '#fb923c'],
    ['#34d399', '#f472b6'],
  ]
  const lines: Array<[ISeriesApi<'Line'>, ISeriesApi<'Line'>]> = palette.map(([k, d]) => [
    stochChart!.addSeries(LineSeries, { color: k, lineWidth: 2, priceLineVisible: false }),
    stochChart!.addSeries(LineSeries, { color: d, lineWidth: 1, lineStyle: LineStyle.Dashed, priceLineVisible: false }),
  ])
  for (const value of [20, 50, 80]) {
    lines[0][0].createPriceLine({
      price: value,
      color: value === 50 ? '#475569' : '#64748b',
      lineWidth: 1,
      lineStyle: LineStyle.Dotted,
      axisLabelVisible: true,
      title: String(value),
    })
  }

  const snapshot = props.snapshot
  candleSeries.setData((snapshot?.candles ?? []).map((candle) => ({
    time: candle.timestamp as UTCTimestamp,
    open: candle.open,
    high: candle.high,
    low: candle.low,
    close: candle.close,
  })))
  for (let group = 0; group < 3; group += 1) {
    lines[group][0].setData((snapshot?.stochastic ?? []).flatMap((point) =>
      point.k[group] == null ? [] : [{ time: point.timestamp as UTCTimestamp, value: point.k[group] }],
    ))
    lines[group][1].setData((snapshot?.stochastic ?? []).flatMap((point) =>
      point.d[group] == null ? [] : [{ time: point.timestamp as UTCTimestamp, value: point.d[group] }],
    ))
  }
  priceChart.timeScale().fitContent()
  stochChart.timeScale().fitContent()

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
  resizeObserver = new ResizeObserver(() => {
    if (!priceContainer.value || !stochContainer.value) return
    priceChart?.applyOptions({ width: priceContainer.value.clientWidth })
    stochChart?.applyOptions({ width: stochContainer.value.clientWidth })
  })
  resizeObserver.observe(priceContainer.value)
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
  priceChart?.timeScale().fitContent()
  stochChart?.timeScale().fitContent()
}

watch(() => props.snapshot, async () => {
  await nextTick()
  buildCharts()
}, { immediate: true })

onBeforeUnmount(() => {
  resizeObserver?.disconnect()
  priceChart?.remove()
  stochChart?.remove()
})
</script>

<template>
  <section class="chart-card" aria-labelledby="market-chart-title">
    <div class="card-heading chart-heading">
      <div>
        <p class="eyebrow">MARKET VIEW</p>
        <h2 id="market-chart-title">BTCUSD 价格与 Stoch</h2>
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
      <summary>查看最近 12 根 OHLC 数据</summary>
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
