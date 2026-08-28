# BTCUSD Backtesting Platform

BTCUSD 历史行情复盘与手动交易练习平台。项目正在从 Windows/C++ 桌面程序迁移到可在单台服务器部署的 Web 服务架构。

> 当前状态：架构文档评审阶段。`services/` 下的代码是尚未按文档验收的实现草稿；文档评审通过前不应作为稳定版本部署。

## 文档索引

| 模块 | 文档 |
| --- | --- |
| 总体架构 | [系统概览](docs/architecture/overview.md) |
| 服务拆分 | [服务边界](docs/architecture/service-boundaries.md) |
| 架构决策 | [关键决策记录](docs/architecture/decisions.md) |
| 本地开发 | [本地开发安装](docs/development/local-setup.md) |
| 前端 | [Vue Web 应用](docs/frontend/web-app.md) |
| 行情接口 | [Market Data API](docs/api/market-data.md) |
| 回测接口 | [Backtest API](docs/api/backtest.md) |
| 数据库 | [PostgreSQL Schema](docs/database/schema.md) |
| 行情文件 | [Parquet 数据规范](docs/data/parquet.md) |
| 部署 | [Docker Compose 与 HTTPS](docs/deployment/docker-compose.md) |
| 运维 | [运行手册](docs/operations/runbook.md) |
| 安全 | [安全边界](docs/security/security.md) |
| 旧系统迁移 | [Windows 客户端迁移](docs/migration/windows-client.md) |
| 验收 | [测试与验收标准](docs/testing/acceptance.md) |

## 目标技术栈

| 层 | 技术 | 作用 |
| --- | --- | --- |
| 前端 | Vue 3 + TypeScript + Vite | 项目库、K 线复盘和手动 Buy/Sell |
| 行情服务 | Rust + Axum | Parquet 查询和周期聚合 |
| 回测服务 | Rust + Axum | 策略执行、配置和回测记录 |
| 数据库 | PostgreSQL | 用户、策略配置、回测状态与结果 |
| 行情文件 | Parquet | BTCUSD 历史 OHLCV |
| Web 入口 | Nginx | HTTPS、静态文件和 API 反向代理 |
| 部署 | Docker Compose | 单台 Linux 服务器部署 |

旧 Win32/C++ 程序在迁移期间保存在 `legacy/windows-desktop/`，仅用于行为对照和数据迁移。
