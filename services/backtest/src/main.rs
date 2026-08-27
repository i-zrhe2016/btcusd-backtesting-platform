mod config;
mod db;
mod engine;
mod error;
mod model;

use axum::{
    extract::{Path, Query, State},
    http::{header, HeaderValue, Method},
    routing::get,
    Json, Router,
};
use config::Config;
use error::ApiError;
use model::{
    BacktestRecord, BacktestRequest, MarketSnapshot, StrategyConfigRecord, StrategyConfigRequest,
};
use serde::Deserialize;
use sqlx::{postgres::PgPoolOptions, PgPool};
use tower_http::{cors::CorsLayer, trace::TraceLayer};
use tracing::info;
use uuid::Uuid;

#[derive(Clone)]
struct AppState {
    pool: PgPool,
    client: reqwest::Client,
    market_data_url: String,
}

#[derive(Deserialize)]
struct ListQuery {
    limit: Option<i64>,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "backtest_service=info,tower_http=info".into()),
        )
        .json()
        .init();
    let config = Config::from_env()?;
    let pool = PgPoolOptions::new()
        .max_connections(10)
        .connect(&config.database_url)
        .await?;
    sqlx::migrate!("./migrations").run(&pool).await?;

    let state = AppState {
        pool,
        client: reqwest::Client::builder()
            .timeout(config.request_timeout)
            .build()?,
        market_data_url: config.market_data_url,
    };
    let app = router(state);
    let listener = tokio::net::TcpListener::bind(config.bind_addr).await?;
    info!(address = %config.bind_addr, "backtest service listening");
    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown_signal())
        .await?;
    Ok(())
}

fn router(state: AppState) -> Router {
    let cors = CorsLayer::new()
        .allow_origin(HeaderValue::from_static("http://localhost:5173"))
        .allow_methods([Method::GET, Method::POST])
        .allow_headers([header::CONTENT_TYPE]);
    Router::new()
        .route("/health", get(health))
        .route("/api/backtests", get(list_backtests).post(create_backtest))
        .route("/api/backtests/{id}", get(get_backtest))
        .route("/api/configs", get(list_configs).post(create_config))
        .with_state(state)
        .layer(cors)
        .layer(TraceLayer::new_for_http())
}

async fn health(State(state): State<AppState>) -> Result<Json<serde_json::Value>, ApiError> {
    sqlx::query("SELECT 1").execute(&state.pool).await?;
    Ok(Json(serde_json::json!({"status": "ok", "service": "backtest"})))
}

async fn create_backtest(
    State(state): State<AppState>,
    Json(request): Json<BacktestRequest>,
) -> Result<Json<BacktestRecord>, ApiError> {
    request.validate().map_err(ApiError::BadRequest)?;
    let config = serde_json::to_value(&request)
        .map_err(|error| ApiError::Internal(error.to_string()))?;
    let running = db::insert_backtest(&state.pool, request.name.trim(), config).await?;

    let response = state
        .client
        .get(format!("{}/api/market/snapshot", state.market_data_url))
        .query(&[
            ("symbol", request.symbol.as_str().to_owned()),
            ("timeframe", request.timeframe.clone()),
            ("from", request.from.to_string()),
            ("to", request.to.to_string()),
            ("limit", "5000".to_owned()),
            ("lengths", format!("{0},{0},{0}", request.strategy.length)),
        ])
        .send()
        .await;
    let response = match response {
        Ok(response) => response,
        Err(error) => {
            db::fail_backtest(&state.pool, running.id, &error.to_string()).await;
            return Err(ApiError::MarketData(error.to_string()));
        }
    };
    if !response.status().is_success() {
        let status = response.status();
        let body = response.text().await.unwrap_or_default();
        let message = format!("market service returned {status}: {body}");
        db::fail_backtest(&state.pool, running.id, &message).await;
        return Err(ApiError::MarketData(message));
    }
    let market: MarketSnapshot = response.json().await.map_err(|error| {
        ApiError::MarketData(format!("invalid market service response: {error}"))
    })?;
    let result = match engine::run(&request, &market) {
        Ok(result) => result,
        Err(error) => {
            db::fail_backtest(&state.pool, running.id, &error.to_string()).await;
            return Err(error);
        }
    };
    let result = serde_json::to_value(result)
        .map_err(|error| ApiError::Internal(error.to_string()))?;
    Ok(Json(
        db::complete_backtest(&state.pool, running.id, result).await?,
    ))
}

async fn list_backtests(
    State(state): State<AppState>,
    Query(query): Query<ListQuery>,
) -> Result<Json<Vec<BacktestRecord>>, ApiError> {
    let limit = query.limit.unwrap_or(20).clamp(1, 100);
    Ok(Json(db::list_backtests(&state.pool, limit).await?))
}

async fn get_backtest(
    State(state): State<AppState>,
    Path(id): Path<Uuid>,
) -> Result<Json<BacktestRecord>, ApiError> {
    db::find_backtest(&state.pool, id)
        .await?
        .map(Json)
        .ok_or_else(|| ApiError::NotFound(id.to_string()))
}

async fn list_configs(
    State(state): State<AppState>,
) -> Result<Json<Vec<StrategyConfigRecord>>, ApiError> {
    Ok(Json(db::list_configs(&state.pool).await?))
}

async fn create_config(
    State(state): State<AppState>,
    Json(request): Json<StrategyConfigRequest>,
) -> Result<Json<StrategyConfigRecord>, ApiError> {
    if request.name.trim().is_empty() || request.name.chars().count() > 120 {
        return Err(ApiError::BadRequest(
            "name must contain between 1 and 120 characters".to_owned(),
        ));
    }
    let parameters = serde_json::to_value(request.parameters)
        .map_err(|error| ApiError::Internal(error.to_string()))?;
    Ok(Json(
        db::create_config(&state.pool, request.name.trim(), parameters).await?,
    ))
}

async fn shutdown_signal() {
    let ctrl_c = async {
        tokio::signal::ctrl_c()
            .await
            .expect("failed to install Ctrl+C handler");
    };
    #[cfg(unix)]
    let terminate = async {
        tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
            .expect("failed to install signal handler")
            .recv()
            .await;
    };
    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();
    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }
}
