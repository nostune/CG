#pragma once
#include <chrono>

namespace outer_wilds {

class TimeManager {
public:
    static TimeManager& GetInstance() {
        static TimeManager instance;
        return instance;
    }

    void Update() {
        auto currentTime = std::chrono::high_resolution_clock::now();
        if (m_LastTime.time_since_epoch().count() > 0) {
            std::chrono::duration<float> elapsed = currentTime - m_LastTime;
            m_DeltaTime = elapsed.count();
            m_TotalTime += m_DeltaTime;
        }
        m_LastTime = currentTime;
    }

    float GetDeltaTime() const { return m_DeltaTime; }
    float GetTotalTime() const { return m_TotalTime; }

private:
    TimeManager() = default;
    
    std::chrono::high_resolution_clock::time_point m_LastTime;
    float m_DeltaTime = 0.0f;
    float m_TotalTime = 0.0f;
};

} // namespace outer_wilds
