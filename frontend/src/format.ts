const currency = new Intl.NumberFormat('zh-CN', {
  style: 'currency',
  currency: 'USD',
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
})

const decimal = new Intl.NumberFormat('zh-CN', {
  minimumFractionDigits: 2,
  maximumFractionDigits: 2,
})

export function formatCurrency(value: number): string {
  return currency.format(value)
}

export function formatNumber(value: number): string {
  return decimal.format(value)
}

export function formatPercent(value: number): string {
  return `${value >= 0 ? '+' : ''}${decimal.format(value)}%`
}

export function formatUtc(timestamp: number): string {
  return new Intl.DateTimeFormat('zh-CN', {
    timeZone: 'UTC',
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
  }).format(new Date(timestamp * 1000))
}

export function toUtcInput(timestamp: number): string {
  return new Date(timestamp * 1000).toISOString().slice(0, 16)
}

export function fromUtcInput(value: string): number {
  return Math.floor(new Date(`${value}:00Z`).getTime() / 1000)
}

