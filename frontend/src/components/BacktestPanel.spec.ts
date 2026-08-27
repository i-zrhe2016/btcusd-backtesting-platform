import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import BacktestPanel from './BacktestPanel.vue'

describe('BacktestPanel', () => {
  it('emits a normalized request for a valid form', async () => {
    const wrapper = mount(BacktestPanel, {
      props: { timeframe: '1h', from: 100, to: 200, running: false, record: null },
    })
    await wrapper.get('form').trigger('submit')
    const request = wrapper.emitted('submit')?.[0]?.[0]
    expect(request).toMatchObject({
      symbol: 'BTCUSD',
      timeframe: '1h',
      from: 100,
      to: 200,
      initial_capital: 10_000,
      fee_rate: 0.001,
      strategy: { length: 30, oversold: 20, overbought: 80 },
    })
  })

  it('blocks submission when the time range is invalid', async () => {
    const wrapper = mount(BacktestPanel, {
      props: { timeframe: '1h', from: 200, to: 100, running: false, record: null },
    })
    expect(wrapper.text()).toContain('开始时间必须早于结束时间')
    expect(wrapper.get('button[type="submit"]').attributes('disabled')).toBeDefined()
    await wrapper.get('form').trigger('submit')
    expect(wrapper.emitted('submit')).toBeUndefined()
  })
})

