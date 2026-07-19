#include "data_feed.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <istream>
#include <limits>
#include <sstream>
#include <string_view>

namespace {

std::string Trim(std::string value) {
    const auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool ParseNumber(const std::string& text, double* value) {
    if (text.empty()) {
        return false;
    }

    try {
        std::size_t offset = 0;
        const double parsed = std::stod(text, &offset);
        if (offset != text.size()) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseInteger(const std::string& text, std::int64_t* value) {
    if (text.empty()) {
        return false;
    }

    try {
        std::size_t offset = 0;
        const std::int64_t parsed = std::stoll(text, &offset);
        if (offset != text.size()) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool IsDigits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

std::int64_t ToUnixTimestampUtc(std::tm time_info) {
#ifdef _WIN32
    return static_cast<std::int64_t>(_mkgmtime(&time_info));
#else
    return static_cast<std::int64_t>(timegm(&time_info));
#endif
}

bool ParseTimestamp(const std::string& text, std::int64_t* timestamp) {
    const std::string value = Trim(text);
    if (value.empty()) {
        return false;
    }

    if (IsDigits(value)) {
        std::int64_t numeric = 0;
        if (!ParseInteger(value, &numeric)) {
            return false;
        }
        if (value.size() >= 13) {
            numeric /= 1000;
        }
        *timestamp = numeric;
        return true;
    }

    std::tm time_info{};
    std::istringstream stream_a(value);
    stream_a >> std::get_time(&time_info, "%Y-%m-%d %H:%M:%S");
    if (!stream_a.fail()) {
        *timestamp = ToUnixTimestampUtc(time_info);
        return true;
    }

    time_info = {};
    std::istringstream stream_b(value);
    stream_b >> std::get_time(&time_info, "%Y-%m-%dT%H:%M:%S");
    if (!stream_b.fail()) {
        *timestamp = ToUnixTimestampUtc(time_info);
        return true;
    }

    time_info = {};
    std::istringstream stream_c(value);
    stream_c >> std::get_time(&time_info, "%Y-%m-%d");
    if (!stream_c.fail()) {
        *timestamp = ToUnixTimestampUtc(time_info);
        return true;
    }

    return false;
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (char ch : line) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            continue;
        }
        if (ch == ',' && !in_quotes) {
            fields.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    fields.push_back(Trim(current));
    return fields;
}

std::tm GmTime(std::int64_t timestamp) {
    const std::time_t raw_time = static_cast<std::time_t>(timestamp);
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, &raw_time);
#else
    gmtime_r(&raw_time, &result);
#endif
    return result;
}

std::int64_t WeekBucketStart(std::int64_t timestamp) {
    constexpr std::int64_t kDaySeconds = 24 * 60 * 60;
    const std::int64_t days = timestamp / kDaySeconds;
    const std::int64_t monday_based_weekday = (days + 3) % 7;
    return (days - monday_based_weekday) * kDaySeconds;
}

std::int64_t MonthBucketStart(std::int64_t timestamp) {
    std::tm month = GmTime(timestamp);
    month.tm_mday = 1;
    month.tm_hour = 0;
    month.tm_min = 0;
    month.tm_sec = 0;
    return ToUnixTimestampUtc(month);
}

std::int64_t BucketStart(std::int64_t timestamp, Timeframe timeframe) {
    switch (timeframe) {
        case Timeframe::M1:
            return (timestamp / 60) * 60;
        case Timeframe::M15:
            return (timestamp / (15 * 60)) * (15 * 60);
        case Timeframe::M30:
            return (timestamp / (30 * 60)) * (30 * 60);
        case Timeframe::H1:
            return (timestamp / 3600) * 3600;
        case Timeframe::H2:
            return (timestamp / (2 * 3600)) * (2 * 3600);
        case Timeframe::H4:
            return (timestamp / (4 * 3600)) * (4 * 3600);
        case Timeframe::D1:
            return (timestamp / (24 * 3600)) * (24 * 3600);
        case Timeframe::W1:
            return WeekBucketStart(timestamp);
        case Timeframe::MN1:
            return MonthBucketStart(timestamp);
    }

    return timestamp;
}

}  // namespace

std::wstring TimeframeLabel(Timeframe timeframe) {
    switch (timeframe) {
        case Timeframe::M1:
            return L"1m";
        case Timeframe::M15:
            return L"15m";
        case Timeframe::M30:
            return L"30m";
        case Timeframe::H1:
            return L"1h";
        case Timeframe::H2:
            return L"2h";
        case Timeframe::H4:
            return L"4h";
        case Timeframe::D1:
            return L"D";
        case Timeframe::W1:
            return L"W";
        case Timeframe::MN1:
            return L"M";
    }

    return L"?";
}

std::vector<Candle> LoadCandlesFromStream(std::istream& input, std::string* error) {
    std::vector<Candle> candles;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }

        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const std::vector<std::string> fields = SplitCsvLine(trimmed);
        if (fields.size() < 5) {
            continue;
        }

        Candle candle;
        if (!ParseTimestamp(fields[0], &candle.timestamp)) {
            continue;
        }
        if (!ParseNumber(fields[1], &candle.open) ||
            !ParseNumber(fields[2], &candle.high) ||
            !ParseNumber(fields[3], &candle.low) ||
            !ParseNumber(fields[4], &candle.close)) {
            continue;
        }
        if (candle.high < candle.low) {
            std::swap(candle.high, candle.low);
        }

        candles.push_back(candle);
    }

    std::sort(candles.begin(), candles.end(), [](const Candle& left, const Candle& right) {
        return left.timestamp < right.timestamp;
    });

    if (candles.empty() && error != nullptr) {
        *error = "CSV parsing produced no candles. Expected columns: timestamp,open,high,low,close";
    }

    return candles;
}

std::vector<Candle> LoadCandlesFromCsv(const std::wstring& path, std::string* error) {
    std::ifstream input{std::filesystem::path(path)};
    if (!input.is_open()) {
        if (error != nullptr) {
            *error = "Unable to open CSV file.";
        }
        return {};
    }

    return LoadCandlesFromStream(input, error);
}

std::vector<Candle> LoadCandlesFromCsvText(const std::string& csv_text, std::string* error) {
    std::istringstream input(csv_text);
    return LoadCandlesFromStream(input, error);
}

std::vector<Candle> AggregateCandles(const std::vector<Candle>& input, Timeframe timeframe) {
    if (input.empty()) {
        return {};
    }

    std::vector<Candle> output;
    output.reserve(input.size());

    Candle current{};
    std::int64_t current_bucket = std::numeric_limits<std::int64_t>::min();

    for (const Candle& item : input) {
        const std::int64_t bucket = BucketStart(item.timestamp, timeframe);
        if (output.empty() || bucket != current_bucket) {
            current_bucket = bucket;
            current = item;
            current.timestamp = bucket;
            output.push_back(current);
            continue;
        }

        Candle& merged = output.back();
        merged.high = std::max(merged.high, item.high);
        merged.low = std::min(merged.low, item.low);
        merged.close = item.close;
    }

    return output;
}

std::vector<Candle> GenerateDemoCandles() {
    std::vector<Candle> candles;
    candles.reserve(24 * 180);

    constexpr std::int64_t kHour = 60 * 60;
    const std::int64_t start = 1735689600;  // 2025-01-01 00:00:00 UTC
    double previous_close = 42000.0;

    for (int i = 0; i < 24 * 180; ++i) {
        const double trend = static_cast<double>(i) * 9.5;
        const double wave_a = std::sin(static_cast<double>(i) / 12.0) * 550.0;
        const double wave_b = std::cos(static_cast<double>(i) / 37.0) * 900.0;
        const double drift = std::sin(static_cast<double>(i) / 5.0) * 120.0;
        const double close = 43000.0 + trend + wave_a + wave_b + drift;
        const double open = previous_close;
        const double high = std::max(open, close) + 120.0 + std::fabs(std::sin(static_cast<double>(i) / 3.0) * 180.0);
        const double low = std::min(open, close) - 120.0 - std::fabs(std::cos(static_cast<double>(i) / 4.0) * 160.0);

        candles.push_back(Candle{
            start + static_cast<std::int64_t>(i) * kHour,
            open,
            high,
            low,
            close,
        });

        previous_close = close;
    }

    return candles;
}

std::wstring FormatTimestamp(std::int64_t timestamp) {
    const std::tm time_info = GmTime(timestamp);
    std::wostringstream stream;
    stream << std::put_time(&time_info, L"%Y-%m-%d %H:%M");
    return stream.str();
}
