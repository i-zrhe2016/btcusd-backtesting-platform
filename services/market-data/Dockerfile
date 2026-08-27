FROM rust:1.88-slim-bookworm AS builder
WORKDIR /app
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY Cargo.toml Cargo.lock ./
COPY services/market-data services/market-data
COPY services/backtest services/backtest
RUN cargo build --locked --release -p market-data-service

FROM debian:bookworm-slim AS runtime
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*
RUN useradd --create-home --uid 10001 app
COPY --from=builder /app/target/release/market-data-service /usr/local/bin/market-data-service
USER app
EXPOSE 8081
ENTRYPOINT ["market-data-service"]
