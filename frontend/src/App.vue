<script setup lang="ts">
import { computed, onMounted, reactive, ref } from 'vue'
import { Activity, Database, RefreshCw, ServerCog } from '@lucide/vue'
import BacktestPanel from './components/BacktestPanel.vue'
import HistoryPanel from './components/HistoryPanel.vue'
import MarketChart from './components/MarketChart.vue'
import { createBacktest, getMarketMetadata, getMarketSnapshot, listBacktests } from './api'
import { fromUtcInput, formatNumber, formatUtc, toUtcInput } from './format'
import type { BacktestRecord, BacktestRequest, MarketMetadata, MarketSnapshot } from './types'

const metadata = ref<MarketMetadata | null>(null)
const snapshot = ref<MarketSnapshot | null>(null)
const history = ref<BacktestRecord[]>([])
const lastRecord = ref<BacktestRecord | null>(null)
const marketLoading = ref(true)
const historyLoading = ref(true)
const backtestRunning = ref(false)
const error = ref('')
const notice = ref('')

const filters = reactive({
  timeframe: '1h',
  fromInput: '',
  toInput: '',
  lengths: [30, 120, 840] as [number, number, number],
})

const timeframeLabels: Record<string, string> = {
  '1m': '1 分', '15m': '15 分', '30m': '30 分', '1h': '1 小时', '2h': '2 小时',
  '4h': '4 小时', '1d': '日', '1w': '周', '1M': '月',
}

const fromTimestamp = computed(() => fromUtcInput(filters.fromInput))
const toTimestamp = computed(() => fromUtcInput(filters.toInput))
const latestCandle = computed(() => snapshot.value?.candles.at(-1) ?? null)

async function loadMarket() {
  if (!filters.fromInput || !filters.toInput || fromTimestamp.value >= toTimestamp.value) {
    error.value = '请选择有效的 UTC 时间范围。'
    return
  }
  marketLoading.value = true
  error.value = ''
  try {
    snapshot.value = await getMarketSnapshot({
      timeframe: filters.timeframe,
      from: fromTimestamp.value,
      to: toTimestamp.value,
      lengths: filters.lengths,
    })
    notice.value = `已加载 ${snapshot.value.candles.length} 根 ${filters.timeframe} K 线。`
  } catch (caught) {
    error.value = caught instanceof Error ? caught.message : '行情加载失败。'
  } finally {
    marketLoading.value = false
  }
}

async function loadHistory() {
  historyLoading.value = true
  try {
    history.value = await listBacktests()
  } catch (caught) {
    error.value = caught instanceof Error ? caught.message : '回测记录加载失败。'
  } finally {
    historyLoading.value = false
  }
}

async function runBacktest(request: BacktestRequest) {
  backtestRunning.value = true
  error.value = ''
  notice.value = '回测已提交，正在计算。'
  try {
    lastRecord.value = await createBacktest(request)
    notice.value = `回测“${lastRecord.value.name}”已完成。`
    await loadHistory()
  } catch (caught) {
    error.value = caught instanceof Error ? caught.message : '回测执行失败。'
  } finally {
    backtestRunning.value = false
  }
}

async function initialize() {
  marketLoading.value = true
  error.value = ''
  try {
    metadata.value = await getMarketMetadata()
    const end = metadata.value.last_timestamp + 60
    const start = Math.max(metadata.value.first_timestamp, end - 90 * 24 * 60 * 60)
    filters.fromInput = toUtcInput(start)
    filters.toInput = toUtcInput(end)
    await Promise.all([loadMarket(), loadHistory()])
  } catch (caught) {
    error.value = caught instanceof Error ? caught.message : '平台初始化失败。'
    marketLoading.value = false
    historyLoading.value = false
  }
}

onMounted(initialize)
</script>

<template>
  <div class="app-shell">
    <header class="topbar">
      <a class="brand" href="/" aria-label="BTCUSD 回测平台首页">
        <span class="brand-mark" aria-hidden="true"><Activity :size="22" /></span>
        <span><strong>BTCUSD</strong><small>BACKTESTING PLATFORM</small></span>
      </a>
      <div class="service-state">
        <span class="status-dot" aria-hidden="true" />
        <span>单机研究环境</span>
      </div>
    </header>

    <main id="main-content" class="workspace" tabindex="-1">
      <section class="hero-strip" aria-labelledby="page-title">
        <div>
          <p class="eyebrow">MARKET RESEARCH / UTC</p>
          <h1 id="page-title">历史行情与策略回测工作台</h1>
          <p>同一份 Parquet 行情驱动图表、指标与回测结果。</p>
        </div>
        <dl class="market-stats">
          <div><dt>最新收盘</dt><dd>{{ latestCandle ? `$${formatNumber(latestCandle.close)}` : '—' }}</dd></div>
          <div><dt>数据总量</dt><dd>{{ metadata ? metadata.candle_count.toLocaleString('zh-CN') : '—' }}</dd></div>
          <div><dt>覆盖至</dt><dd>{{ metadata ? `${formatUtc(metadata.last_timestamp)} UTC` : '—' }}</dd></div>
        </dl>
      </section>

      <div v-if="error" class="alert error-alert" role="alert">
        <ServerCog :size="20" aria-hidden="true" />
        <span>{{ error }}</span>
        <button type="button" class="text-button" @click="initialize">重试</button>
      </div>
      <p class="sr-only" aria-live="polite">{{ notice }}</p>

      <section class="control-card" aria-labelledby="range-title">
        <div class="control-title">
          <Database :size="20" aria-hidden="true" />
          <div><h2 id="range-title">行情范围</h2><span>所有时间均为 UTC</span></div>
        </div>
        <div class="timeframe-tabs" role="group" aria-label="K 线周期">
          <button
            v-for="timeframe in metadata?.supported_timeframes ?? Object.keys(timeframeLabels)"
            :key="timeframe"
            type="button"
            :class="{ active: filters.timeframe === timeframe }"
            :aria-pressed="filters.timeframe === timeframe"
            @click="filters.timeframe = timeframe; loadMarket()"
          >
            {{ timeframeLabels[timeframe] }}
          </button>
        </div>
        <div class="range-fields">
          <label>开始时间<input v-model="filters.fromInput" type="datetime-local" /></label>
          <label>结束时间<input v-model="filters.toInput" type="datetime-local" /></label>
          <button class="secondary-button" type="button" :disabled="marketLoading" @click="loadMarket">
            <RefreshCw :size="17" :class="{ spinning: marketLoading }" aria-hidden="true" />
            {{ marketLoading ? '加载中' : '刷新行情' }}
          </button>
        </div>
      </section>

      <div class="content-grid">
        <MarketChart :snapshot="snapshot" :loading="marketLoading" />
        <aside class="side-column" aria-label="回测控制与历史">
          <BacktestPanel
            :timeframe="filters.timeframe"
            :from="fromTimestamp"
            :to="toTimestamp"
            :running="backtestRunning"
            :record="lastRecord"
            @submit="runBacktest"
          />
          <HistoryPanel :records="history" :loading="historyLoading" />
        </aside>
      </div>
    </main>
  </div>
</template>
