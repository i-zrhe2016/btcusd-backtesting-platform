# Backtest API

## 策略定义

M1 只提供 `stoch-cross-long`：

- 前一根 `K <= D`、当前 `K > D` 且 `K <= oversold` 时，以当前收盘价全仓买入。
- 前一根 `K >= D`、当前 `K < D` 且 `K >= overbought` 时，以当前收盘价全部卖出。
- 只做多，不加杠杆，不允许同时持有多个仓位。
- 买卖均按成交名义金额收取 `fee_rate`，不模拟滑点。
- 结束时未平仓仓位按最后收盘价计入最终权益，但不计为完整交易。

## POST `/api/backtests`

请求：

```json
{
  "name": "1h Stoch 30",
  "symbol": "BTCUSD",
  "timeframe": "1h",
  "from": 1514764800,
  "to": 1546300800,
  "initial_capital": 10000,
  "fee_rate": 0.001,
  "strategy": {
    "length": 30,
    "oversold": 20,
    "overbought": 80
  }
}
```

校验规则：名称 `1..120` 个字符；初始资金大于 0；手续费 `0..0.05`；长度 `2..5000`；`0 <= oversold < overbought <= 100`；时间范围有效且最多处理 5,000 根聚合 K 线。

成功返回完整回测记录：

```json
{
  "id": "1a1f09a7-143d-4c16-af0b-72e7f58f1a5e",
  "user_id": "00000000-0000-0000-0000-000000000001",
  "name": "1h Stoch 30",
  "status": "completed",
  "config": {},
  "result": {
    "initial_capital": 10000,
    "final_equity": 11240.53,
    "total_return_pct": 12.4053,
    "max_drawdown_pct": 8.31,
    "completed_trades": 12,
    "win_rate_pct": 58.33,
    "buy_and_hold_return_pct": 4.12,
    "candles_processed": 876,
    "trades": []
  },
  "error_message": null,
  "created_at": "2026-08-26T10:00:00Z",
  "completed_at": "2026-08-26T10:00:01Z"
}
```

`trades` 中每项包含 `timestamp, side, price, quantity, fee, realized_pnl`。买入的 `realized_pnl` 为 `null`。

## GET `/api/backtests?limit=20`

按 `created_at` 倒序返回当前访客用户的回测记录。`limit` 被限制在 `1..100`。M1 列表返回完整结果；数据量增长后可新增摘要 DTO，但不得无版本地删除现有字段。

## GET `/api/backtests/{id}`

返回当前用户的一条记录。UUID 格式错误返回 400；记录不存在或不属于当前用户时统一返回 404。

## GET `/api/configs`

按更新时间倒序返回当前用户保存的策略配置。

## POST `/api/configs`

```json
{
  "name": "保守参数",
  "parameters": {
    "length": 30,
    "oversold": 15,
    "overbought": 85
  }
}
```

名称和参数使用与回测请求相同的校验规则。M1 配置创建后不可原地覆盖；后续更新接口必须使用显式 `PUT /api/configs/{id}`。

## 失败语义

- 写入初始记录后，状态立即为 `running`。
- 行情超时、无对齐数据或计算失败时更新为 `failed`，写入不含敏感信息的 `error_message`。
- 成功结果与 `completed_at` 在同一次数据库更新中写入。
- Nginx 对同步回测使用 60 秒上游超时；超时不等于服务端任务已取消，客户端应刷新记录状态。

