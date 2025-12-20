#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <chrono>
#include "TimeManager.h"

namespace outer_wilds {

class DebugManager {
public:
    static DebugManager& GetInstance() {
        static DebugManager instance;
        return instance;
    }

    void Log(const std::string& message) {
        m_DebugMessages.push_back(message);
    }

    void Log(const std::string& category, const std::string& message) {
        Log("[" + category + "] " + message);
    }

    void Update(float deltaTime) {
        // 定期调试输出已禁用
        // m_TimeAccumulator += deltaTime;
        // if (m_TimeAccumulator >= m_DebugInterval) {
        //     PrintAllMessages();
        //     m_DebugMessages.clear();
        //     m_TimeAccumulator = 0.0f;
        // }
    }
    
    void SetShowFPS(bool show) { m_ShowFPS = show; }
    bool IsShowingFPS() const { return m_ShowFPS; }

    void ForcePrint() {
        PrintAllMessages();
        m_DebugMessages.clear();
        m_TimeAccumulator = 0.0f;
    }

    void SetDebugInterval(float interval) {
        m_DebugInterval = interval;
    }

private:
    DebugManager() = default;
    ~DebugManager() = default;
    DebugManager(const DebugManager&) = delete;
    DebugManager& operator=(const DebugManager&) = delete;

    void PrintAllMessages() {
        if (!m_DebugMessages.empty() || m_ShowFPS) {
            std::cout << "\n=== Debug Info ===" << std::endl;
            
            // 显示FPS
            if (m_ShowFPS) {
                float fps = TimeManager::GetInstance().GetFPS();
                std::cout << "[FPS] " << static_cast<int>(fps) << " fps (" 
                          << (1000.0f / fps) << " ms/frame)" << std::endl;
            }
            
            // 显示其他消息
            for (const auto& message : m_DebugMessages) {
                std::cout << message << std::endl;
            }
            std::cout << "==================\n" << std::endl;
        }
    }

    std::vector<std::string> m_DebugMessages;
    float m_TimeAccumulator = 0.0f;
    float m_DebugInterval = 2.0f; // 每2秒输出一次
    bool m_ShowFPS = false;
};

} // namespace outer_wilds