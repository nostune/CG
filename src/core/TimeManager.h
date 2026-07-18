#pragma once
#include <algorithm>
#include <chrono>

namespace outer_wilds {

class TimeManager {
public:
    static TimeManager& GetInstance() {
        static TimeManager instance;
        return instance;
    }

    void Update() {
        const auto currentTime = Clock::now();
        if (m_LastTime.time_since_epoch().count() > 0) {
            std::chrono::duration<float> elapsed = currentTime - m_LastTime;
            m_UnscaledDeltaTime = (std::max)(0.0f, elapsed.count());
            m_DeltaTime = (std::min)(m_UnscaledDeltaTime, MAX_FRAME_DELTA);
            m_TotalTime += m_DeltaTime;
            
            // 更新FPS计算
            m_FrameCount++;
            m_FpsAccumulator += m_DeltaTime;
            if (m_FpsAccumulator >= 1.0f) {
                m_CurrentFPS = m_FrameCount / m_FpsAccumulator;
                m_FrameCount = 0;
                m_FpsAccumulator = 0.0f;
            }
        }
        m_LastTime = currentTime;
    }

    float GetDeltaTime() const { return m_DeltaTime; }
    float GetUnscaledDeltaTime() const { return m_UnscaledDeltaTime; }
    float GetTotalTime() const { return m_TotalTime; }
    float GetFPS() const { return m_CurrentFPS; }

private:
    TimeManager() = default;

    using Clock = std::chrono::steady_clock;
    static constexpr float MAX_FRAME_DELTA = 1.0f / 15.0f;
    
    Clock::time_point m_LastTime;
    float m_DeltaTime = 0.0f;
    float m_UnscaledDeltaTime = 0.0f;
    float m_TotalTime = 0.0f;
    
    // FPS计算
    int m_FrameCount = 0;
    float m_FpsAccumulator = 0.0f;
    float m_CurrentFPS = 0.0f;
};

} // namespace outer_wilds
