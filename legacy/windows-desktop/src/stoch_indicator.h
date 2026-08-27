#pragma once

#include "data_feed.h"

#include <array>
#include <cstddef>
#include <vector>

// Parameters for one chart timeframe. The values mirror the defaults in
// stoch_btc_v9_k5_optimized.
struct StochParameters {
    std::array<int, 5> lengths{};
};

struct StochSeries {
    std::array<std::vector<double>, 5> k;
    std::array<std::vector<double>, 5> d;
};

StochParameters DefaultStochParameters(Timeframe timeframe);
StochSeries CalculateStochSeries(const std::vector<Candle>& candles,
                                 const StochParameters& parameters);

