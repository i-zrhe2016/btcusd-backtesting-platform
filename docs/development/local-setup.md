# 本地开发安装

本模块只说明开发机如何安装依赖、启动服务和验证最小闭环。生产部署、HTTPS、备份和发布流程见 [Docker Compose 与 HTTPS](../deployment/docker-compose.md)。

本地开发拓扑图：[PlantUML 源码](../diagrams/local-development.puml) · [SVG](../diagrams/local-development.svg)

![本地开发拓扑](../diagrams/local-development.svg)

## 前置条件

| 依赖 | 用途 | 要求 |
| --- | --- | --- |
| Rust/Cargo | 编译并运行两个 Axum 服务 | 使用当前稳定版；容器构建以 `rust:1.88-slim-bookworm` 为基线 |
| Node.js/npm | 安装前端依赖并运行 Vite | 使用 Node 24 或与前端 Dockerfile 一致的主版本 |
| Docker | 启动本地 PostgreSQL | 只用于开发数据库 |
| BTCUSD Parquet | 行情服务启动数据源 | 符合 [Parquet 行情数据规范](../data/parquet.md) |
| curl | 健康检查 | 系统包管理器安装即可 |

行情服务在监听端口前会读取 Parquet。没有 `btcusd_1m.parquet` 时，前端可以启动，但行情和回测闭环无法完成。

## 目录约定

开发机推荐使用以下本地路径，避免和生产容器路径混淆：

```bash
mkdir -p data/market
```

将行情文件放到：

```text
data/market/btcusd_1m.parquet
```

该目录不应提交 Git。行情文件版本、覆盖范围和 SHA-256 记录方式见 [Parquet 行情数据规范](../data/parquet.md)。

如果只需要本地 Compose 冒烟，可以生成最近几小时的 Coinbase BTC-USD 小样本：

```bash
cargo run -p market-data-service --example fetch_coinbase_fixture -- data/market/btcusd_1m.parquet
```

该命令会同时写入 `data/market/manifest.json`。它只用于开发验证，不替代生产历史数据构建流程。

## 安装依赖

在仓库根目录安装前端依赖：

```bash
cd frontend
npm ci
cd ..
```

Rust 依赖由 Cargo 在首次构建时自动解析：

```bash
cargo build --workspace
```

如果只想验证单个服务：

```bash
cargo build -p market-data-service
cargo build -p backtest-service
```

## 启动 PostgreSQL

本地开发使用固定弱密码即可；不要复用到生产环境。

```bash
docker run --name btcusd-postgres \
  -e POSTGRES_DB=btcusd \
  -e POSTGRES_USER=btcusd \
  -e POSTGRES_PASSWORD=btcusd \
  -p 5432:5432 \
  -d postgres:17-alpine
```

已存在同名容器时：

```bash
docker start btcusd-postgres
```

回测服务启动时会执行 `services/backtest/migrations` 中的 SQL 迁移，并写入固定本地访客用户。

## 启动 Market Data

新开一个终端，在仓库根目录运行：

```bash
export MARKET_DATA_BIND=127.0.0.1:8081
export MARKET_DATA_PARQUET="$PWD/data/market/btcusd_1m.parquet"
cargo run -p market-data-service
```

启动成功后验证：

```bash
curl http://127.0.0.1:8081/health
curl http://127.0.0.1:8081/api/market/meta
```

如果进程在监听前退出，优先检查 Parquet 路径、列名、列类型和文件是否为空。

## 启动 Backtest

新开一个终端，在仓库根目录运行：

```bash
export BACKTEST_BIND=127.0.0.1:8082
export DATABASE_URL=postgres://btcusd:btcusd@127.0.0.1:5432/btcusd
export MARKET_DATA_URL=http://127.0.0.1:8081
cargo run -p backtest-service
```

启动成功后验证：

```bash
curl http://127.0.0.1:8082/health
curl http://127.0.0.1:8082/api/backtests
curl http://127.0.0.1:8082/api/configs
```

`/health` 会查询 PostgreSQL。它失败时，不要先改代码，先确认数据库容器状态和 `DATABASE_URL`。

## 启动前端

新开一个终端：

```bash
cd frontend
npm run dev
```

浏览器访问：

```text
http://localhost:5173
```

Vite 已配置开发代理：

| 前端路径 | 代理目标 |
| --- | --- |
| `/api/market/*` | `http://localhost:8081` |
| `/api/backtests*` | `http://localhost:8082` |
| `/api/configs*` | `http://localhost:8082` |

因此本地开发不需要 Nginx，也不需要 HTTPS 证书。

## 最小冒烟检查

按顺序确认：

1. `market-data` 日志显示已加载行情记录数。
2. `curl http://127.0.0.1:8081/api/market/meta` 返回 `BTCUSD`、覆盖时间和支持周期。
3. `backtest` 日志显示迁移完成并监听 `127.0.0.1:8082`。
4. `curl http://127.0.0.1:8082/health` 返回 `status: ok`。
5. 前端页面能加载行情图表，回测请求不返回 502。

回测失败时优先看 `backtests.error_message` 和回测服务日志；不要直接删除失败记录。

## 常见问题

### `market-data` 无法打开 Parquet

- 确认 `MARKET_DATA_PARQUET` 是绝对路径或从仓库根目录可解析的相对路径。
- 确认文件列名是 `timestamp, open, high, low, close, volume`。
- 确认 `timestamp` 为 Unix UTC 秒，不是毫秒。

### `backtest` 连接不上数据库

- 确认 `docker ps` 中存在 `btcusd-postgres`。
- 确认本机 `5432` 没有被另一个 PostgreSQL 占用。
- 确认 `DATABASE_URL` 使用 `127.0.0.1`，不是生产 Compose 内的 `postgres` 主机名。

### 前端请求失败

- 直接访问两个后端健康检查，先排除后端未启动。
- 确认 Vite 运行在 `frontend/` 目录。
- 确认浏览器访问的是 `http://localhost:5173`，不是生产 Nginx 地址。

### 生产 Docker 配置如何验证

生产部署使用仓库根目录的 `docker-compose.yml`、`frontend/Dockerfile` 和 `deployment/nginx/`。先执行 `docker compose config` 校验配置；完整启动还需要 `data/market/btcusd_1m.parquet` 和生产 `.env`。
