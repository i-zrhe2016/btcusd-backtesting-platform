use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use serde::Serialize;

#[derive(Debug, thiserror::Error)]
pub enum ApiError {
    #[error("invalid request: {0}")]
    BadRequest(String),
    #[error("backtest {0} was not found")]
    NotFound(String),
    #[error("market data request failed: {0}")]
    MarketData(String),
    #[error("database operation failed")]
    Database(#[from] sqlx::Error),
    #[error("internal service error: {0}")]
    Internal(String),
}

#[derive(Serialize)]
struct ErrorBody {
    code: &'static str,
    message: String,
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (status, code) = match &self {
            Self::BadRequest(_) => (StatusCode::BAD_REQUEST, "invalid_request"),
            Self::NotFound(_) => (StatusCode::NOT_FOUND, "not_found"),
            Self::MarketData(_) => (StatusCode::BAD_GATEWAY, "market_data_error"),
            Self::Database(_) | Self::Internal(_) => {
                (StatusCode::INTERNAL_SERVER_ERROR, "internal_error")
            }
        };
        if matches!(self, Self::Database(_)) {
            tracing::error!(error = ?self, "database request failed");
        }
        let message = self.to_string();
        (status, Json(ErrorBody { code, message })).into_response()
    }
}

