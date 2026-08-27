use std::{env, net::SocketAddr, path::PathBuf};

#[derive(Debug, Clone)]
pub struct Config {
    pub bind_addr: SocketAddr,
    pub parquet_path: PathBuf,
}

impl Config {
    pub fn from_env() -> anyhow::Result<Self> {
        let bind_addr = env::var("MARKET_DATA_BIND")
            .unwrap_or_else(|_| "0.0.0.0:8081".to_owned())
            .parse()?;
        let parquet_path = env::var("MARKET_DATA_PARQUET")
            .unwrap_or_else(|_| "/data/btcusd_1m.parquet".to_owned())
            .into();

        Ok(Self {
            bind_addr,
            parquet_path,
        })
    }
}

