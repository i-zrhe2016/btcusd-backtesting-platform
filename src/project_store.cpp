#include "project_store.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(),
        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string EncodeText(const std::wstring& value) {
    const std::string bytes = WideToUtf8(value);
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (const unsigned char byte : bytes) {
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '/' ||
            byte == '\\' || byte == '-' || byte == '_' || byte == ' ') {
            output << static_cast<char>(byte);
        } else {
            output << '%' << std::setw(2) << static_cast<int>(byte) << std::setw(0);
        }
    }
    return output.str();
}

int HexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::wstring DecodeText(const std::string& value) {
    std::string bytes;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '%' && index + 2 < value.size()) {
            const int high = HexValue(value[index + 1]);
            const int low = HexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                bytes.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        bytes.push_back(value[index]);
    }
    return Utf8ToWide(bytes);
}

std::wstring JoinPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty()) return right;
    if (left.back() == L'\\') return left + right;
    return left + L'\\' + right;
}

bool EnsureDirectory(const std::wstring& path, std::wstring* error) {
    if (CreateDirectoryW(path.c_str(), nullptr) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    if (error != nullptr) {
        *error = L"无法创建目录：" + path + L"。请确认程序目录可写。";
    }
    return false;
}

bool ParseInt64(const std::string& text, std::int64_t* value) {
    try {
        std::size_t offset = 0;
        const long long parsed = std::stoll(text, &offset);
        if (offset != text.size()) return false;
        *value = static_cast<std::int64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseInt(const std::string& text, int* value) {
    try {
        std::size_t offset = 0;
        const int parsed = std::stoi(text, &offset);
        if (offset != text.size()) return false;
        *value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseDouble(const std::string& text, double* value) {
    try {
        std::size_t offset = 0;
        const double parsed = std::stod(text, &offset);
        if (offset != text.size()) return false;
        *value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseBool(const std::string& text, bool* value) {
    int parsed = 0;
    if (!ParseInt(text, &parsed)) return false;
    *value = parsed != 0;
    return true;
}

std::vector<std::string> Split(const std::string& value, char separator) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(separator, begin);
        result.push_back(value.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

void WriteLine(std::ostringstream& output, const char* key, const std::string& value) {
    output << key << '=' << value << '\n';
}

std::string BoolText(bool value) { return value ? "1" : "0"; }

std::string SerializeProject(const ProjectRecord& project) {
    std::ostringstream output;
    output << "# BTCUSD Replay project\n";
    WriteLine(output, "version", std::to_string(project.version));
    WriteLine(output, "id", EncodeText(project.id));
    WriteLine(output, "name", EncodeText(project.name));
    WriteLine(output, "builtin_source", BoolText(project.builtin_source));
    WriteLine(output, "source_relative_path", EncodeText(project.source_relative_path));
    WriteLine(output, "start_timestamp", std::to_string(project.start_timestamp));
    WriteLine(output, "end_exclusive_timestamp", std::to_string(project.end_exclusive_timestamp));
    WriteLine(output, "playback_timestamp", std::to_string(project.playback_timestamp));
    WriteLine(output, "timeframe", std::to_string(project.timeframe));
    WriteLine(output, "speed_index", std::to_string(project.speed_index));
    WriteLine(output, "zoom_quarters", std::to_string(project.zoom_quarters));
    WriteLine(output, "stoch_visible", BoolText(project.stoch_visible));
    WriteLine(output, "status_visible", BoolText(project.status_visible));
    WriteLine(output, "stoch_panel_height", std::to_string(project.stoch_panel_height));
    for (std::size_t index = 0; index < project.stoch_lengths.size(); ++index) {
        std::ostringstream values;
        for (std::size_t group = 0; group < project.stoch_lengths[index].size(); ++group) {
            if (group != 0) values << ',';
            values << project.stoch_lengths[index][group];
        }
        WriteLine(output, ("stoch_lengths_" + std::to_string(index)).c_str(), values.str());
    }
    for (std::size_t index = 0; index < project.shortcuts.size(); ++index) {
        WriteLine(output, ("shortcut_" + std::to_string(index)).c_str(),
            std::to_string(project.shortcuts[index]));
    }
    for (const StoredTrendline& trendline : project.trendlines) {
        std::ostringstream values;
        values << trendline.start_timestamp << ',' << std::setprecision(17) << trendline.start_price
               << ',' << trendline.end_timestamp << ',' << trendline.end_price;
        WriteLine(output, "trendline", values.str());
    }
    for (const StoredMarker& marker : project.markers) {
        std::ostringstream values;
        values << marker.timestamp << ',' << std::setprecision(17) << marker.price
               << ',' << BoolText(marker.buy);
        WriteLine(output, "marker", values.str());
    }
    for (const StoredWindow& window : project.windows) {
        std::ostringstream values;
        values << EncodeText(window.id) << ',' << window.timeframe << ',' << window.left << ','
               << window.top << ',' << window.width << ',' << window.height << ','
               << window.zoom_quarters << ',' << BoolText(window.follow_playback) << ','
               << BoolText(window.stoch_visible) << ',' << BoolText(window.status_visible) << ','
               << window.stoch_panel_height;
        WriteLine(output, "window", values.str());
    }
    return output.str();
}

bool DeserializeProject(const std::string& text, ProjectRecord* project) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "version") ParseInt(value, &project->version);
        else if (key == "id") project->id = DecodeText(value);
        else if (key == "name") project->name = DecodeText(value);
        else if (key == "builtin_source") ParseBool(value, &project->builtin_source);
        else if (key == "source_relative_path") project->source_relative_path = DecodeText(value);
        else if (key == "start_timestamp") ParseInt64(value, &project->start_timestamp);
        else if (key == "end_exclusive_timestamp") ParseInt64(value, &project->end_exclusive_timestamp);
        else if (key == "playback_timestamp") ParseInt64(value, &project->playback_timestamp);
        else if (key == "timeframe") ParseInt(value, &project->timeframe);
        else if (key == "speed_index") ParseInt(value, &project->speed_index);
        else if (key == "zoom_quarters") ParseInt(value, &project->zoom_quarters);
        else if (key == "stoch_visible") ParseBool(value, &project->stoch_visible);
        else if (key == "status_visible") ParseBool(value, &project->status_visible);
        else if (key == "stoch_panel_height") ParseInt(value, &project->stoch_panel_height);
        else if (key.rfind("stoch_lengths_", 0) == 0) {
            int index = 0;
            if (ParseInt(key.substr(14), &index) && index >= 0 && index < 9) {
                const auto parts = Split(value, ',');
                for (std::size_t group = 0; group < parts.size() && group < 5; ++group) {
                    ParseInt(parts[group], &project->stoch_lengths[static_cast<std::size_t>(index)][group]);
                }
            }
        } else if (key.rfind("shortcut_", 0) == 0) {
            int index = 0;
            if (ParseInt(key.substr(9), &index) && index >= 0 && index < 17) {
                int value_int = 0;
                if (ParseInt(value, &value_int)) project->shortcuts[static_cast<std::size_t>(index)] = static_cast<unsigned int>(value_int);
            }
        } else if (key == "trendline") {
            const auto parts = Split(value, ',');
            if (parts.size() == 4) {
                StoredTrendline trendline;
                if (ParseInt64(parts[0], &trendline.start_timestamp) && ParseDouble(parts[1], &trendline.start_price) &&
                    ParseInt64(parts[2], &trendline.end_timestamp) && ParseDouble(parts[3], &trendline.end_price)) {
                    project->trendlines.push_back(trendline);
                }
            }
        } else if (key == "marker") {
            const auto parts = Split(value, ',');
            if (parts.size() == 3) {
                StoredMarker marker;
                if (ParseInt64(parts[0], &marker.timestamp) && ParseDouble(parts[1], &marker.price) && ParseBool(parts[2], &marker.buy)) {
                    project->markers.push_back(marker);
                }
            }
        } else if (key == "window") {
            const auto parts = Split(value, ',');
            if (parts.size() == 11) {
                StoredWindow window;
                window.id = DecodeText(parts[0]);
                ParseInt(parts[1], &window.timeframe); ParseInt(parts[2], &window.left);
                ParseInt(parts[3], &window.top); ParseInt(parts[4], &window.width);
                ParseInt(parts[5], &window.height); ParseInt(parts[6], &window.zoom_quarters);
                ParseBool(parts[7], &window.follow_playback); ParseBool(parts[8], &window.stoch_visible);
                ParseBool(parts[9], &window.status_visible); ParseInt(parts[10], &window.stoch_panel_height);
                project->windows.push_back(window);
            }
        }
    }
    return !project->id.empty() && !project->name.empty();
}

}  // namespace

std::wstring ApplicationDirectory() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    std::wstring result(path, path + length);
    const std::size_t separator = result.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"." : result.substr(0, separator);
}

std::wstring ProjectsDirectory() { return JoinPath(ApplicationDirectory(), L"projects"); }
std::wstring DataSourcesDirectory() { return JoinPath(JoinPath(ApplicationDirectory(), L"data"), L"sources"); }
std::wstring ProjectPath(const std::wstring& project_id) { return JoinPath(ProjectsDirectory(), project_id + L".replay"); }

bool EnsureProjectDirectories(std::wstring* error) {
    const std::wstring root = ApplicationDirectory();
    return !root.empty() && EnsureDirectory(JoinPath(root, L"projects"), error) &&
        EnsureDirectory(JoinPath(root, L"data"), error) &&
        EnsureDirectory(DataSourcesDirectory(), error);
}

std::vector<std::wstring> ListProjectIds() {
    std::vector<std::wstring> result;
    WIN32_FIND_DATAW data{};
    const std::wstring pattern = JoinPath(ProjectsDirectory(), L"*.replay");
    const HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return result;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            std::wstring name(data.cFileName);
            if (name.size() > 7 && name.substr(name.size() - 7) == L".replay") {
                result.push_back(name.substr(0, name.size() - 7));
            }
        }
    } while (FindNextFileW(handle, &data) != FALSE);
    FindClose(handle);
    std::sort(result.begin(), result.end());
    return result;
}

bool ReadBinaryFile(const std::wstring& path, std::string* text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == FALSE || size.QuadPart < 0 || size.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    text->assign(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = text->empty() || ReadFile(file, text->data(), static_cast<DWORD>(text->size()), &read, nullptr);
    CloseHandle(file);
    return ok != FALSE && read == text->size();
}

bool LoadProject(const std::wstring& project_id, ProjectRecord* project, std::wstring* error) {
    if (project == nullptr) return false;
    std::string text;
    if (!ReadBinaryFile(ProjectPath(project_id), &text)) {
        if (error != nullptr) *error = L"无法打开项目文件。";
        return false;
    }
    if (!DeserializeProject(text, project)) {
        if (error != nullptr) *error = L"项目文件格式无效。";
        return false;
    }
    return true;
}

bool SaveProject(const ProjectRecord& project, std::wstring* error) {
    if (!EnsureProjectDirectories(error)) return false;
    const std::wstring path = ProjectPath(project.id);
    const std::wstring temporary = path + L".tmp";
    const std::string bytes = SerializeProject(project);
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error != nullptr) *error = L"无法写入项目文件，请确认程序目录可写。";
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if (ok == FALSE || written != bytes.size() ||
        MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        DeleteFileW(temporary.c_str());
        if (error != nullptr) *error = L"保存项目文件失败。";
        return false;
    }
    return true;
}

bool DeleteProject(const std::wstring& project_id, std::wstring* error) {
    if (DeleteFileW(ProjectPath(project_id).c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    if (error != nullptr) *error = L"删除项目失败。";
    return false;
}

bool ReadLastProjectId(std::wstring* project_id) {
    if (project_id == nullptr) return false;
    std::string text;
    if (!ReadBinaryFile(JoinPath(ProjectsDirectory(), L"last-project"), &text)) return false;
    const std::size_t newline = text.find_first_of("\r\n");
    if (newline != std::string::npos) text.resize(newline);
    *project_id = DecodeText(text);
    return !project_id->empty();
}

bool WriteLastProjectId(const std::wstring& project_id, std::wstring* error) {
    if (!EnsureProjectDirectories(error)) return false;
    const std::string bytes = EncodeText(project_id) + "\n";
    HANDLE file = CreateFileW(JoinPath(ProjectsDirectory(), L"last-project").c_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error != nullptr) *error = L"无法保存最近项目记录。";
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == bytes.size();
}

bool CopyCsvIntoDataDirectory(const std::wstring& source_path, const std::wstring& project_id,
    std::wstring* relative_path, std::wstring* error) {
    if (!EnsureProjectDirectories(error)) return false;
    const std::wstring filename = L"source-" + project_id + L".csv";
    const std::wstring destination = JoinPath(DataSourcesDirectory(), filename);
    if (CopyFileW(source_path.c_str(), destination.c_str(), FALSE) == FALSE) {
        if (error != nullptr) *error = L"无法将 CSV 复制到程序 data\\sources 目录。";
        return false;
    }
    if (relative_path != nullptr) *relative_path = L"data\\sources\\" + filename;
    return true;
}
