#include "stoch_indicator.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

int SmoothLength(int length) {
    // Pine: max(1, int(round(length / 10.0 * 3.0))). All supplied defaults
    // are multiples of ten, but keeping the exact rule also covers edits to
    // the parameters later.
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(length) / 10.0 * 3.0)));
}

std::vector<double> SimpleMovingAverage(const std::vector<double>& values, int length) {
    std::vector<double> result(values.size(), kNaN);
    if (length <= 0 || values.empty()) {
        return result;
    }

    double sum = 0.0;
    int valid_count = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double value = values[i];
        if (std::isfinite(value)) {
            sum += value;
            ++valid_count;
        }

        if (i >= static_cast<std::size_t>(length)) {
            const double old_value = values[i - static_cast<std::size_t>(length)];
            if (std::isfinite(old_value)) {
                sum -= old_value;
                --valid_count;
            }
        }

        // ta.sma propagates the initial unavailable range here because the
        // raw stochastic value is unavailable until its lookback is filled.
        if (i + 1 >= static_cast<std::size_t>(length) && valid_count == length) {
            result[i] = sum / static_cast<double>(length);
        }
    }
    return result;
}

std::vector<double> CalculatePair(const std::vector<Candle>& candles, int length,
                                  std::vector<double>* d_values) {
    const std::size_t count = candles.size();
    const int safe_length = std::max(1, length);
    const int smooth_length = SmoothLength(safe_length);
    std::vector<double> raw_k(count, kNaN);
    std::deque<std::size_t> lowest_indices;
    std::deque<std::size_t> highest_indices;

    for (std::size_t i = 0; i < count; ++i) {
        while (!lowest_indices.empty() && candles[lowest_indices.back()].close >= candles[i].close) {
            lowest_indices.pop_back();
        }
        lowest_indices.push_back(i);
        while (!highest_indices.empty() && candles[highest_indices.back()].close <= candles[i].close) {
            highest_indices.pop_back();
        }
        highest_indices.push_back(i);

        const std::size_t first = i + 1 > static_cast<std::size_t>(safe_length)
            ? i + 1 - static_cast<std::size_t>(safe_length)
            : 0;
        while (!lowest_indices.empty() && lowest_indices.front() < first) {
            lowest_indices.pop_front();
        }
        while (!highest_indices.empty() && highest_indices.front() < first) {
            highest_indices.pop_front();
        }

        if (i + 1 < static_cast<std::size_t>(safe_length)) {
            continue;
        }

        const double lowest_close = candles[lowest_indices.front()].close;
        const double highest_close = candles[highest_indices.front()].close;
        const double price_range = highest_close - lowest_close;
        raw_k[i] = price_range == 0.0
            ? 50.0
            : (candles[i].close - lowest_close) / price_range * 100.0;
    }

    std::vector<double> k_values = SimpleMovingAverage(raw_k, smooth_length);
    *d_values = SimpleMovingAverage(k_values, smooth_length);
    return k_values;
}

}  // namespace

StochParameters DefaultStochParameters(Timeframe timeframe) {
    switch (timeframe) {
        case Timeframe::M1:
            return {{30, 120, 840, 240, 840}};
        case Timeframe::M15:
            return {{40, 160, 960, 80, 160}};
        case Timeframe::M30:
            return {{30, 120, 480, 480, 1680}};
        case Timeframe::H1:
            return {{30, 120, 840, 240, 840}};
        case Timeframe::H2:
            return {{30, 120, 840, 420, 840}};
        case Timeframe::H4:
            return {{30, 210, 840, 840, 1680}};
        case Timeframe::D1:
            return {{10, 70, 280, 280, 840}};
        case Timeframe::W1:
            return {{10, 40, 120, 480, 960}};
        case Timeframe::MN1:
            return {{10, 30, 120, 240, 480}};
    }

    return {{30, 120, 840, 240, 840}};
}

StochSeries CalculateStochSeries(const std::vector<Candle>& candles,
                                 const StochParameters& parameters) {
    StochSeries series;
    // The replay UI intentionally exposes the first three groups only.
    for (std::size_t group = 0; group < std::min<std::size_t>(3, parameters.lengths.size()); ++group) {
        series.k[group] = CalculatePair(candles, parameters.lengths[group], &series.d[group]);
    }
    return series;
}
