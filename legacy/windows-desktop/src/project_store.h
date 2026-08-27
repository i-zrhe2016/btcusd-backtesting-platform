#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct StoredTrendline {
    std::int64_t start_timestamp = 0;
    double start_price = 0.0;
    std::int64_t end_timestamp = 0;
    double end_price = 0.0;
};

struct StoredMarker {
    std::int64_t timestamp = 0;
    double price = 0.0;
    bool buy = true;
};

struct StoredWindow {
    std::wstring id;
    int timeframe = 0;
    int left = 0;
    int top = 0;
    int width = 1100;
    int height = 700;
    int zoom_quarters = 4;
    bool follow_playback = true;
    bool stoch_visible = true;
    bool status_visible = true;
    int stoch_panel_height = 190;
};

struct ProjectRecord {
    int version = 2;
    std::wstring id;
    std::wstring name;
    bool builtin_source = true;
    std::wstring source_relative_path;
    std::int64_t start_timestamp = 0;
    std::int64_t end_exclusive_timestamp = 0;
    std::int64_t playback_timestamp = 0;
    int timeframe = 0;
    int speed_index = 0;
    int zoom_quarters = 4;
    bool stoch_visible = true;
    bool status_visible = true;
    int stoch_panel_height = 190;
    std::array<std::array<int, 5>, 9> stoch_lengths{};
    std::array<unsigned int, 17> shortcuts{};
    std::vector<StoredTrendline> trendlines;
    std::vector<StoredMarker> markers;
    std::vector<StoredWindow> windows;
};

std::wstring ApplicationDirectory();
std::wstring ProjectsDirectory();
std::wstring DataSourcesDirectory();
std::wstring ProjectPath(const std::wstring& project_id);
bool EnsureProjectDirectories(std::wstring* error);
std::vector<std::wstring> ListProjectIds();
bool LoadProject(const std::wstring& project_id, ProjectRecord* project, std::wstring* error);
bool SaveProject(const ProjectRecord& project, std::wstring* error);
bool DeleteProject(const std::wstring& project_id, std::wstring* error);
bool ReadLastProjectId(std::wstring* project_id);
bool WriteLastProjectId(const std::wstring& project_id, std::wstring* error);
bool CopyCsvIntoDataDirectory(const std::wstring& source_path,
    const std::wstring& project_id,
    std::wstring* relative_path,
    std::wstring* error);
