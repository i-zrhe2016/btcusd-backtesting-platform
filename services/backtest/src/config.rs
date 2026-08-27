use std::{env, net::SocketAddr, time::Duration};

#[derive(Clone)]
pub struct Config {
    pub bind_addr: SocketAddr,
    pub database_url: String,
    pub market_data_url: String,
    pub request_timeout: Duration,
}

impl Config {
    pub fn from_env() -> anyhow::Result<Self> {
        Ok(Self {
            bind_addr: env::var("BACKTEST_BIND")
                .unwrap_or_else(|_| "0.0.0.0:8082".to_owned())
                .parse()?,
            database_url: env::var("DATABASE_URL")
                .unwrap_or_else(|_| "postgres://btcusd:btcusd@postgres:5432/btcusd".to_owned()),
            market_data_url: env::var("MARKET_DATA_URL")
                .unwrap_or_else(|_| "http://market-data:8081".to_owned())
                .trim_end_matches('/')
                .to_owned(),
            request_timeout: Duration::from_secs(
                env::var("MARKET_DATA_TIMEOUT_SECONDS")
                    .ok()
                    .and_then(|value| value.parse().ok())
                    .unwrap_or(30),
            ),
        })
    }
}

