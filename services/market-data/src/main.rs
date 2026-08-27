mod config;
mod error;
mod indicator;
mod market;
mod model;
mod parquet_store;

use std::sync::Arc;

use axum::{
    extract::{Query, State},
    http::{header, HeaderValue, Method},
    routing::get,
    Json, Router,
};
use config::Config;
use error::ApiError;
use model::{Candle, MarketMetadata, MarketSnapshot, SnapshotQuery};
use tower_http::{cors::CorsLayer, trace::TraceLayer};
use tracing::info;

#[derive(Clone)]
struct AppState {
    candles: Arc<Vec<Candle>>,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "market_data_service=info,tower_http=info".into()),
        )
        .json()
        .init();

    let config = Config::from_env()?;
    let parquet_path = config.parquet_path.clone();
    let candles = tokio::task::spawn_blocking(move || parquet_store::load_candles(&parquet_path))
        .await??;
    info!(count = candles.len(), "loaded market data");

    let app = router(AppState {
        candles: Arc::new(candles),
    });
    let listener = tokio::net::TcpListener::bind(config.bind_addr).await?;
    info!(address = %config.bind_addr, "market data service listening");
    axum::serve(listener, app)
        .with_graceful_shutdown(shutdown_signal())
        .await?;
    Ok(())
}

fn router(state: AppState) -> Router {
    let cors = CorsLayer::new()
        .allow_origin(HeaderValue::from_static("http://localhost:5173"))
        .allow_methods([Method::GET])
        .allow_headers([header::CONTENT_TYPE]);

    Router::new()
        .route("/health", get(health))
        .route("/api/market/meta", get(metadata))
        .route("/api/market/snapshot", get(snapshot))
        .with_state(state)
        .layer(cors)
        .layer(TraceLayer::new_for_http())
}

async fn health() -> Json<serde_json::Value> {
    Json(serde_json::json!({"status": "ok", "service": "market-data"}))
}

async fn metadata(State(state): State<AppState>) -> Json<MarketMetadata> {
    let first_timestamp = state.candles.first().map_or(0, |candle| candle.timestamp);
    let last_timestamp = state.candles.last().map_or(0, |candle| candle.timestamp);
    Json(MarketMetadata {
        symbol: "BTCUSD",
        base_timeframe: "1m",
        source: "Parquet",
        candle_count: state.candles.len(),
        first_timestamp,
        last_timestamp,
        supported_timeframes: ["1m", "15m", "30m", "1h", "2h", "4h", "1d", "1w", "1M"],
    })
}

async fn snapshot(
    State(state): State<AppState>,
    Query(query): Query<SnapshotQuery>,
) -> Result<Json<MarketSnapshot>, ApiError> {
    let candles = state.candles.clone();
    let snapshot = tokio::task::spawn_blocking(move || market::snapshot(&candles, query))
        .await
        .map_err(|error| ApiError::Unavailable(error.to_string()))??;
    Ok(Json(snapshot))
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

