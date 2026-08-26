# Market Data API

## 通用约定

- 公网前缀：`/api/market`
- 内容类型：`application/json`
- 时间：Unix UTC 秒，范围为左闭右开 `[from, to)`。
- 唯一支持的品种：`BTCUSD`，同时接受输入别名 `BTC-USD`。
- 支持周期：`1m, 15m, 30m, 1h, 2h, 4h, 1d, 1w, 1M`。

错误格式：

```json
{
  "code": "invalid_request",
  "message": "invalid request: limit must be between 1 and 5000"
}
```

## GET `/api/market/meta`

返回当前数据集信息，供页面初始化时间范围。

```json
{
  "symbol": "BTCUSD",
  "base_timeframe": "1m",
  "source": "Parquet",
  "candle_count": 1540000,
  "first_timestamp": 1451606400,
  "last_timestamp": 1546300740,
  "supported_timeframes": ["1m", "15m", "30m", "1h", "2h", "4h", "1d", "1w", "1M"]
}
```

## GET `/api/market/snapshot`

查询参数：

| 参数 | 必填 | 默认值 | 规则 |
| --- | --- | --- | --- |
| `symbol` | 否 | `BTCUSD` | 仅 BTCUSD |
| `timeframe` | 否 | `1h` | 使用支持周期之一，大小写敏感 |
| `from` | 否 | 数据开始 | Unix 秒 |
| `to` | 否 | 数据结束后 | Unix 秒且大于 `from` |
| `limit` | 否 | `500` | `1..5000`；超出范围返回 400 |
| `lengths` | 否 | `30,120,840` | 三个 `2..5000` 的逗号分隔整数 |

响应中的 `candles` 与 `stochastic` 按 `timestamp` 一一对齐。指标预热区用 JSON `null`，不输出 `NaN`。

```json
{
  "symbol": "BTCUSD",
  "timeframe": "1h",
  "source": "Parquet",
  "candles": [
    {
      "timestamp": 1530403200,
      "open": 6385.1,
      "high": 6412.0,
      "low": 6350.0,
      "close": 6401.2,
      "volume": 312.45
    }
  ],
  "stochastic": [
    {
      "timestamp": 1530403200,
      "k": [22.41, null, null],
      "d": [19.87, null, null]
    }
  ]
}
```

如果聚合结果超过 `limit`，返回时间范围内最新的 `limit` 根。请求范围无数据时返回 200 和两个空数组，前端显示空状态。

## GET `/health`

内部健康探针。服务完成 Parquet 加载后才开始监听；正常响应为：

```json
{"status":"ok","service":"market-data"}
```

Nginx 不向公网代理此路径。

