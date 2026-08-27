use serde_json::Value;
use sqlx::PgPool;
use uuid::Uuid;

use crate::model::{BacktestRecord, StrategyConfigRecord, GUEST_USER_ID};

pub async fn insert_backtest(
    pool: &PgPool,
    name: &str,
    config: Value,
) -> Result<BacktestRecord, sqlx::Error> {
    sqlx::query_as::<_, BacktestRecord>(
        r#"
        INSERT INTO backtests (user_id, name, status, config)
        VALUES ($1, $2, 'running', $3)
        RETURNING id, user_id, name, status, config, result, error_message, created_at, completed_at
        "#,
    )
    .bind(GUEST_USER_ID)
    .bind(name)
    .bind(config)
    .fetch_one(pool)
    .await
}

pub async fn complete_backtest(
    pool: &PgPool,
    id: Uuid,
    result: Value,
) -> Result<BacktestRecord, sqlx::Error> {
    sqlx::query_as::<_, BacktestRecord>(
        r#"
        UPDATE backtests
        SET status = 'completed', result = $2, completed_at = now()
        WHERE id = $1
        RETURNING id, user_id, name, status, config, result, error_message, created_at, completed_at
        "#,
    )
    .bind(id)
    .bind(result)
    .fetch_one(pool)
    .await
}

pub async fn fail_backtest(pool: &PgPool, id: Uuid, message: &str) {
    if let Err(error) = sqlx::query(
        "UPDATE backtests SET status = 'failed', error_message = $2, completed_at = now() WHERE id = $1",
    )
    .bind(id)
    .bind(message)
    .execute(pool)
    .await
    {
        tracing::error!(%error, %id, "failed to persist failed backtest state");
    }
}

pub async fn list_backtests(pool: &PgPool, limit: i64) -> Result<Vec<BacktestRecord>, sqlx::Error> {
    sqlx::query_as::<_, BacktestRecord>(
        r#"
        SELECT id, user_id, name, status, config, result, error_message, created_at, completed_at
        FROM backtests
        WHERE user_id = $1
        ORDER BY created_at DESC
        LIMIT $2
        "#,
    )
    .bind(GUEST_USER_ID)
    .bind(limit)
    .fetch_all(pool)
    .await
}

pub async fn find_backtest(pool: &PgPool, id: Uuid) -> Result<Option<BacktestRecord>, sqlx::Error> {
    sqlx::query_as::<_, BacktestRecord>(
        r#"
        SELECT id, user_id, name, status, config, result, error_message, created_at, completed_at
        FROM backtests
        WHERE id = $1 AND user_id = $2
        "#,
    )
    .bind(id)
    .bind(GUEST_USER_ID)
    .fetch_optional(pool)
    .await
}

pub async fn list_configs(pool: &PgPool) -> Result<Vec<StrategyConfigRecord>, sqlx::Error> {
    sqlx::query_as::<_, StrategyConfigRecord>(
        r#"
        SELECT id, user_id, name, parameters, created_at, updated_at
        FROM strategy_configs
        WHERE user_id = $1
        ORDER BY updated_at DESC
        "#,
    )
    .bind(GUEST_USER_ID)
    .fetch_all(pool)
    .await
}

pub async fn create_config(
    pool: &PgPool,
    name: &str,
    parameters: Value,
) -> Result<StrategyConfigRecord, sqlx::Error> {
    sqlx::query_as::<_, StrategyConfigRecord>(
        r#"
        INSERT INTO strategy_configs (user_id, name, parameters)
        VALUES ($1, $2, $3)
        RETURNING id, user_id, name, parameters, created_at, updated_at
        "#,
    )
    .bind(GUEST_USER_ID)
    .bind(name)
    .bind(parameters)
    .fetch_one(pool)
    .await
}

