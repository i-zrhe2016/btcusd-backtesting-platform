<script setup lang="ts">
import { Clock3 } from '@lucide/vue'
import type { BacktestRecord } from '../types'
import { formatPercent } from '../format'

defineProps<{
  records: BacktestRecord[]
  loading: boolean
}>()

function createdAt(value: string): string {
  return new Intl.DateTimeFormat('zh-CN', {
    timeZone: 'UTC',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
  }).format(new Date(value))
}
</script>

<template>
  <section class="side-card history-card" aria-labelledby="history-title">
    <div class="card-heading">
      <div>
        <p class="eyebrow">RECENT RUNS</p>
        <h2 id="history-title">回测记录</h2>
      </div>
      <Clock3 :size="22" class="heading-icon" aria-hidden="true" />
    </div>
    <div v-if="loading" class="history-loading">正在读取记录…</div>
    <p v-else-if="!records.length" class="history-empty">尚无回测记录。完成第一次回测后会显示在这里。</p>
    <ol v-else class="history-list">
      <li v-for="record in records" :key="record.id">
        <div>
          <strong>{{ record.name }}</strong>
          <span>{{ createdAt(record.created_at) }} UTC</span>
        </div>
        <span v-if="record.result" :class="record.result.total_return_pct >= 0 ? 'positive' : 'negative'">
          {{ formatPercent(record.result.total_return_pct) }}
        </span>
        <span v-else class="status-pill" :data-status="record.status">{{ record.status }}</span>
      </li>
    </ol>
  </section>
</template>
