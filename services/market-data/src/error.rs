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
    #[error("market data is unavailable: {0}")]
    Unavailable(String),
}

#[derive(Serialize)]
struct ErrorBody {
    code: &'static str,
    message: String,
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (status, code) = match self {
            Self::BadRequest(_) => (StatusCode::BAD_REQUEST, "invalid_request"),
            Self::Unavailable(_) => (StatusCode::SERVICE_UNAVAILABLE, "market_data_unavailable"),
        };
        let message = self.to_string();
        (status, Json(ErrorBody { code, message })).into_response()
    }
}

