# BTCUSD Replay Tool

一个运行在 Windows 上的纯 C++ BTCUSD 复盘工具，使用 Win32 API + GDI 实现，不依赖 Qt、SDL 或浏览器壳。

支持功能：

- BTCUSD K 线回放
- 播放 / 暂停
- 倍速播放：`x1 / x2 / x4 / x8`
- 周期切换：`1H / 4H / D / W`
- 拖动进度条定位
- `CSV` 文件导入
- 无文件时自动加载内置真实 `BTC-USD` 小时线数据

## 内置真实数据

当前 exe 已内置官方交易所的 `BTC-USD` 1 小时 K 线数据：

- 来源：`Coinbase Exchange`
- 覆盖范围：`2019-01-01 00:00:00 UTC` 到 `2026-03-06 16:00:00 UTC`
- 基础周期：`1H`
- 程序中的 `4H / D / W` 都由内置 `1H` 数据聚合得到

生成脚本：

```powershell
python tools\fetch_coinbase_btcusd.py
```

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
- 触发：`push(main)`、`pull_request`、手动 `workflow_dispatch`
- 环境：`windows-latest` + `MSYS2 MINGW64` + `CMake`
- 产物：artifact `btcusd-replay-windows-x64`，包含 `BtcUsdReplay.exe` 和对应的 `SHA256`

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
- `1H / 4H / D / W`：切换 K 线周期
- 进度条：拖动跳转

快捷键：

- `Space`：播放 / 暂停
- `Left / Right`：前后单步
- `1 / 4 / D / W`：切换周期
- `S`：切换倍速
