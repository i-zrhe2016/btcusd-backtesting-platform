# BTCUSD Replay Tool

一个运行在 Windows 上的纯 C++ BTCUSD 复盘工具，使用 Win32 API + GDI 实现，不依赖 Qt、SDL 或浏览器壳。

支持功能：

- BTCUSD K 线回放
- 播放 / 暂停
- 倍速播放：`x1 / x2 / x4 / x8`
- 周期切换：`1m / 15m / 30m / 1h / 2h / 4h / D / W / M`
- 输入日期并点击 `Go Date` 定位到指定日期
- 在价格图或 Stoch 图上滚动鼠标滚轮缩放，缩放级别显示在状态栏
- 在价格图中按住鼠标左键左右拖拽，可手动平移 K 线视图；播放或单步后恢复跟随回放
- `CSV` 文件导入
- `New Window`：打开一个独立复盘窗口，不同窗口可以加载不同 CSV
- 无文件时自动加载内置真实 `BTC-USD` 2016-01-01 至 2019-01-01 的 1 分钟数据
- `stoch_btc_v9_k5_optimized` 随机指标副图
- 按周期修改五组随机指标参数
- 可配置播放、单步、周期切换、倍速和指标快捷键

## 内置真实数据

当前 exe 已内置官方交易所的 `BTC-USD` 1 分钟 K 线数据：

- 来源：`Coinbase Exchange`
- 覆盖范围：`2016-01-01 00:00 UTC` 至 `2018-12-31 23:59 UTC`
- 基础周期：`1m`
- 程序中的 `15m / 30m / 1h / 2h / 4h / D / W / M` 都由内置 `1m` 数据聚合得到
- Coinbase 历史数据中的缺失分钟保留为空档，程序不会用模拟数据填补

生成脚本：

```powershell
python tools\fetch_coinbase_btcusd.py
```

脚本每次固定生成 `2016-01-01` 至 `2019-01-01` 的 1 分钟数据，并将结果写入
`src/embedded_btcusd_data.h`。Coinbase 每次最多返回 300 根 1 分钟 K 线，脚本会分片抓取、重试并去重。

## CSV 格式

按以下列顺序读取前 5 列：

```csv
timestamp,open,high,low,close
2025-01-01 00:00:00,42000,42120,41880,42080
2025-01-01 01:00:00,42080,42210,42010,42155
```

时间字段支持：

- Unix 秒时间戳
- Unix 毫秒时间戳
- `YYYY-MM-DD HH:MM:SS`
- `YYYY-MM-DDTHH:MM:SS`
- `YYYY-MM-DD`

## Windows 构建

使用 Visual Studio 2022：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

生成后的程序：

```text
build\Release\BtcUsdReplay.exe
```

## GitHub Actions

仓库已包含 Windows 编译工作流：

- 文件：`.github/workflows/build.yml`
- 触发：`push(main)`、`push tag(v*)`、`pull_request`、手动 `workflow_dispatch`
- 环境：`windows-latest` + `MSYS2 MINGW64` + `CMake`
- 产物：artifact `btcusd-replay-windows-x64`，包含 `BtcUsdReplay.exe` 和对应的 `SHA256`
- Release：推送如 `v1.0.0` 这样的 tag 后，会自动创建或更新同名 GitHub Release，并上传 `exe` 与 `SHA256`
- Latest：每次推送 `main` 并成功构建后，会自动更新 `latest` Release，可下载最新 exe 与 `SHA256`

## 用法

直接运行：

```powershell
.\build\Release\BtcUsdReplay.exe
```

不传文件时会优先使用 exe 内置的真实 `BTC-USD` 数据。

带 CSV 启动：

```powershell
.\build\Release\BtcUsdReplay.exe path\to\btcusd.csv
```

## 操作

- `Open CSV`：加载数据
- `Play / Pause`：开始或暂停回放
- `< / >`：单根前进或后退
- `Speed`：切换倍速
- `1m / 15m / 30m / 1h / 2h / 4h / D / W / M`：切换 K 线周期
- `Stoch 3`：显示或隐藏随机指标副图，指标固定显示前三组
- `Stoch Params`：修改当前周期的五组 `length` 参数，点击 `Apply` 后立即重算；参数按周期分别保存
- `Hotkeys`：修改播放、前后单步、倍速、趋势线、随机指标显示和全部周期切换快捷键
- Buy / Sell 快捷键：在当前回放 K 线收盘价记录并绘制 Buy / Sell 标记，默认分别为 `B` / `V`
- `Status On / Status Off`：显示或隐藏底部整体状态栏
- `Date` + `Go Date`：输入 `YYYY-MM-DD`，定位到该日期当天的第一根 K 线
- 拖动价格图和指标图之间的横向分隔条：调整指标副图高度

快捷键：

- `Space`：播放 / 暂停
- `Left / Right`：前后单步
- 默认周期快捷键：`6 / 5 / 3 / 1 / 2 / 4 / D / W / M`，分别切换 `1m / 15m / 30m / 1h / 2h / 4h / D / W / M`
- `S`：切换倍速
- `T`：切换趋势线绘制模式
- `O`：显示或隐藏随机指标
- `B`：记录 Buy 标记
- `V`：记录 Sell 标记

## Stoch 指标

内置的 `stoch_btc_v9_k5_optimized` 使用与 Pine 脚本相同的计算方式：

```text
raw K = (close - lowest(close, length)) /
        (highest(close, length) - lowest(close, length)) * 100
K = SMA(raw K, round(length / 10 * 3))
D = SMA(K, round(length / 10 * 3))
```

价格区间不变时 `raw K` 使用 50。副图固定绘制前三组 `k1/d1`、`k2/d2`、`k3/d3`，并绘制 20、50、80 三条参考线。当前支持的图表周期使用以下 Pine 参数：

| 周期 | k1 | k2 | k3 | k4 | k5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1m | 30 | 120 | 840 | 240 | 840 |
| 15m | 40 | 160 | 960 | 80 | 160 |
| 30m | 30 | 120 | 480 | 480 | 1680 |
| 1h | 30 | 120 | 840 | 240 | 840 |
| 2h | 30 | 120 | 840 | 420 | 840 |
| 4h | 30 | 210 | 840 | 840 | 1680 |
| D | 10 | 70 | 280 | 280 | 840 |
| W | 10 | 40 | 120 | 480 | 960 |
| M | 10 | 30 | 120 | 240 | 480 |

内置数据源是 1m，因此所有界面周期都可以由真实的 1m 数据聚合得到。导入高周期 CSV 时，程序仍不会把高周期 K 线伪拆分成低周期数据。
