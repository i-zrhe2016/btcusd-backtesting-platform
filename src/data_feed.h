#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class Timeframe {
    H1,
    H4,
    D1,
    W1,
};

struct Candle {
    std::int64_t timestamp = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
};

std::wstring TimeframeLabel(Timeframe timeframe);
std::vector<Candle> LoadCandlesFromCsv(const std::wstring& path, std::string* error);
std::vector<Candle> LoadCandlesFromCsvText(const std::string& csv_text, std::string* error);
std::vector<Candle> AggregateCandles(const std::vector<Candle>& input, Timeframe timeframe);
std::vector<Candle> GenerateDemoCandles();
std::wstring FormatTimestamp(std::int64_t timestamp);
