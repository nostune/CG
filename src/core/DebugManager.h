#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace outer_wilds {

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

struct LogEntry {
    std::uint64_t sequence = 0;
    double secondsSinceStart = 0.0;
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
};

struct DebugMetric {
    std::string name;
    double value = 0.0;
    std::string unit;
};

class DebugManager {
public:
    static DebugManager& GetInstance();

    bool Initialize(const std::filesystem::path& logDirectory);
    void Shutdown();
    void Update(float deltaTime);

    void Log(const std::string& message);
    void Log(const std::string& category, const std::string& message);
    void Log(LogLevel level, const std::string& category, const std::string& message);
    void Trace(const std::string& category, const std::string& message);
    void Debug(const std::string& category, const std::string& message);
    void Info(const std::string& category, const std::string& message);
    void Warning(const std::string& category, const std::string& message);
    void Error(const std::string& category, const std::string& message);
    void Critical(const std::string& category, const std::string& message);

    std::vector<LogEntry> GetEntries() const;
    void ClearEntries();
    std::size_t GetEntryCount() const;
    std::uint64_t GetDroppedEntryCount() const;

    void SetMetric(const std::string& name, double value, const std::string& unit = {});
    std::vector<DebugMetric> GetMetrics() const;

    void SetMinimumLevel(LogLevel level);
    LogLevel GetMinimumLevel() const;
    void SetConsoleEnabled(bool enabled);
    bool IsConsoleEnabled() const;
    void SetShowFPS(bool show);
    bool IsShowingFPS() const;

    const std::filesystem::path& GetSessionLogPath() const;
    static const char* LevelName(LogLevel level);

private:
    DebugManager() = default;
    ~DebugManager();
    DebugManager(const DebugManager&) = delete;
    DebugManager& operator=(const DebugManager&) = delete;

    static constexpr std::size_t MaxEntries = 4000;

    mutable std::mutex m_Mutex;
    std::deque<LogEntry> m_Entries;
    std::unordered_map<std::string, DebugMetric> m_Metrics;
    std::ofstream m_LogFile;
    std::filesystem::path m_SessionLogPath;
    std::chrono::steady_clock::time_point m_StartTime = std::chrono::steady_clock::now();
    std::uint64_t m_NextSequence = 1;
    std::uint64_t m_DroppedEntries = 0;
    LogLevel m_MinimumLevel = LogLevel::Info;
    bool m_ConsoleEnabled = false;
    bool m_ShowFPS = true;
    bool m_Initialized = false;
};

} // namespace outer_wilds
