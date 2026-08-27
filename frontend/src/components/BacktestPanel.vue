<script setup lang="ts">
import { computed, reactive, watch } from 'vue'
import { Play, ShieldCheck } from '@lucide/vue'
import type { BacktestRecord, BacktestRequest } from '../types'
import { formatCurrency, formatPercent } from '../format'

const props = defineProps<{
  timeframe: string
  from: number
  to: number
  running: boolean
  record: BacktestRecord | null
}>()

const emit = defineEmits<{
  submit: [request: BacktestRequest]
}>()

const form = reactive({
  name: 'Stoch 交叉回测',
  initialCapital: 10_000,
  feePercent: 0.1,
  length: 30,
  oversold: 20,
  overbought: 80,
})

watch(() => props.timeframe, (timeframe) => {
  form.name = `${timeframe} Stoch ${form.length}`
}, { immediate: true })

const error = computed(() => {
  if (!form.name.trim()) return '请输入回测名称。'
  if (form.initialCapital <= 0) return '初始资金必须大于 0。'
  if (form.feePercent < 0 || form.feePercent > 5) return '手续费必须在 0% 到 5% 之间。'
  if (form.length < 2 || form.length > 5000) return '指标长度必须在 2 到 5000 之间。'
  if (form.oversold < 0 || form.overbought > 100 || form.oversold >= form.overbought) {
    return '阈值必须满足 0 ≤ 超卖 < 超买 ≤ 100。'
  }
  if (props.from >= props.to) return '行情开始时间必须早于结束时间。'
  return ''
})

function submit() {
  if (error.value || props.running) return
  emit('submit', {
    name: form.name.trim(),
    symbol: 'BTCUSD',
    timeframe: props.timeframe,
    from: props.from,
    to: props.to,
    initial_capital: form.initialCapital,
    fee_rate: form.feePercent / 100,
    strategy: {
      length: form.length,
      oversold: form.oversold,
      overbought: form.overbought,
    },
  })
}
</script>

<template>
  <section class="side-card" aria-labelledby="backtest-title">
    <div class="card-heading">
      <div>
        <p class="eyebrow">STRATEGY LAB</p>
        <h2 id="backtest-title">运行回测</h2>
      </div>
      <ShieldCheck :size="22" class="heading-icon" aria-hidden="true" />
    </div>
    <form class="backtest-form" @submit.prevent="submit">
      <label>
        回测名称
        <input v-model="form.name" name="name" autocomplete="off" maxlength="120" />
      </label>
      <div class="field-grid two">
        <label>
          初始资金（USD）
          <input v-model.number="form.initialCapital" name="capital" type="number" min="1" step="100" />
        </label>
        <label>
          单边手续费（%）
          <input v-model.number="form.feePercent" name="fee" type="number" min="0" max="5" step="0.01" />
        </label>
      </div>
      <label>
        Stoch 长度
        <input v-model.number="form.length" name="length" type="number" min="2" max="5000" />
      </label>
      <div class="field-grid two">
        <label>
          超卖阈值
          <input v-model.number="form.oversold" name="oversold" type="number" min="0" max="99" />
        </label>
        <label>
          超买阈值
          <input v-model.number="form.overbought" name="overbought" type="number" min="1" max="100" />
        </label>
      </div>
      <p class="form-help">低位 K 上穿 D 全仓买入，高位 K 下穿 D 全部卖出；仅做多。</p>
      <p v-if="error" id="backtest-error" class="form-error" role="alert">{{ error }}</p>
      <button class="primary-button" type="submit" :disabled="running || Boolean(error)" aria-describedby="backtest-error">
        <span v-if="running" class="button-spinner" aria-hidden="true" />
        <Play v-else :size="18" aria-hidden="true" />
        {{ running ? '正在计算…' : '开始回测' }}
      </button>
    </form>

    <div v-if="record?.result" class="result-panel" aria-live="polite">
      <div class="result-header">
        <span>最近结果</span>
        <strong :class="record.result.total_return_pct >= 0 ? 'positive' : 'negative'">
          {{ formatPercent(record.result.total_return_pct) }}
        </strong>
      </div>
      <dl class="metric-grid">
        <div><dt>最终权益</dt><dd>{{ formatCurrency(record.result.final_equity) }}</dd></div>
        <div><dt>最大回撤</dt><dd>{{ record.result.max_drawdown_pct.toFixed(2) }}%</dd></div>
        <div><dt>完成交易</dt><dd>{{ record.result.completed_trades }}</dd></div>
        <div><dt>胜率</dt><dd>{{ record.result.win_rate_pct.toFixed(2) }}%</dd></div>
        <div><dt>买入持有</dt><dd>{{ formatPercent(record.result.buy_and_hold_return_pct) }}</dd></div>
        <div><dt>处理 K 线</dt><dd>{{ record.result.candles_processed }}</dd></div>
      </dl>
    </div>
  </section>
</template>
