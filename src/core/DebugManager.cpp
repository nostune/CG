#include "DebugManager.h"

#include "TimeManager.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace outer_wilds {
namespace {

std::string SessionTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime = {};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream stream;
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    stream << std::put_time(&localTime, "%Y%m%d-%H%M%S")
           << '-' << std::setw(3) << std::setfill('0') << milliseconds;
    return stream.str();
}

} // namespace

DebugManager& DebugManager::GetInstance() {
    static DebugManager instance;
    return instance;
}

DebugManager::~DebugManager() {
    Shutdown();
}

bool DebugManager::Initialize(const std::filesystem::path& logDirectory) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Initialized) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(logDirectory, error);
    if (error) {
        return false;
    }

    m_StartTime = std::chrono::steady_clock::now();
    m_SessionLogPath = logDirectory / ("OuterWilds-" + SessionTimestamp() + ".log");
    m_LogFile.open(m_SessionLogPath, std::ios::out | std::ios::trunc);
    m_Initialized = m_LogFile.is_open();
    return m_Initialized;
}

void DebugManager::Shutdown() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_LogFile.is_open()) {
        m_LogFile.flush();
        m_LogFile.close();
    }
    m_Initialized = false;
}

void DebugManager::Update(float deltaTime) {
    SetMetric("Frame time", static_cast<double>(deltaTime) * 1000.0, "ms");
    SetMetric("FPS", TimeManager::GetInstance().GetFPS(), "fps");
}

void DebugManager::Log(const std::string& message) {
    Log(LogLevel::Info, "General", message);
}

void DebugManager::Log(const std::string& category, const std::string& message) {
    Log(LogLevel::Info, category, message);
}

void DebugManager::Log(LogLevel level, const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (level < m_MinimumLevel) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    LogEntry entry;
    entry.sequence = m_NextSequence++;
    entry.secondsSinceStart = std::chrono::duration<double>(now - m_StartTime).count();
    entry.level = level;
    entry.category = category;
    entry.message = message;

    if (m_Entries.size() == MaxEntries) {
        m_Entries.pop_front();
        ++m_DroppedEntries;
    }
    m_Entries.push_back(entry);

    std::ostringstream line;
    line << '[' << std::fixed << std::setprecision(3) << entry.secondsSinceStart << "] "
         << '[' << LevelName(level) << "] "
         << '[' << category << "] " << message;

    if (m_LogFile.is_open()) {
        m_LogFile << line.str() << '\n';
        m_LogFile.flush();
    }
    if (m_ConsoleEnabled) {
        std::ostream& output = level >= LogLevel::Warning ? std::cerr : std::cout;
        output << line.str() << std::endl;
    }
}

void DebugManager::Trace(const std::string& category, const std::string& message) { Log(LogLevel::Trace, category, message); }
void DebugManager::Debug(const std::string& category, const std::string& message) { Log(LogLevel::Debug, category, message); }
void DebugManager::Info(const std::string& category, const std::string& message) { Log(LogLevel::Info, category, message); }
void DebugManager::Warning(const std::string& category, const std::string& message) { Log(LogLevel::Warning, category, message); }
void DebugManager::Error(const std::string& category, const std::string& message) { Log(LogLevel::Error, category, message); }
void DebugManager::Critical(const std::string& category, const std::string& message) { Log(LogLevel::Critical, category, message); }

std::vector<LogEntry> DebugManager::GetEntries() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return {m_Entries.begin(), m_Entries.end()};
}

void DebugManager::ClearEntries() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Entries.clear();
}

std::size_t DebugManager::GetEntryCount() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Entries.size();
}

std::uint64_t DebugManager::GetDroppedEntryCount() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_DroppedEntries;
}

void DebugManager::SetMetric(const std::string& name, double value, const std::string& unit) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Metrics[name] = DebugMetric{name, value, unit};
}

std::vector<DebugMetric> DebugManager::GetMetrics() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    std::vector<DebugMetric> result;
    result.reserve(m_Metrics.size());
    for (const auto& pair : m_Metrics) {
        result.push_back(pair.second);
    }
    std::sort(result.begin(), result.end(), [](const DebugMetric& left, const DebugMetric& right) {
        return left.name < right.name;
    });
    return result;
}

void DebugManager::SetMinimumLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_MinimumLevel = level;
}

LogLevel DebugManager::GetMinimumLevel() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_MinimumLevel;
}

void DebugManager::SetConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_ConsoleEnabled = enabled;
}

bool DebugManager::IsConsoleEnabled() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_ConsoleEnabled;
}

void DebugManager::SetShowFPS(bool show) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_ShowFPS = show;
}

bool DebugManager::IsShowingFPS() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_ShowFPS;
}

const std::filesystem::path& DebugManager::GetSessionLogPath() const {
    return m_SessionLogPath;
}

const char* DebugManager::LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

} // namespace outer_wilds
