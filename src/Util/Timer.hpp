#pragma once

#include <chrono>

// Timer 用于统计一段代码从起点到当前时刻经过的时间。
class Timer {
private:
    using Clock = std::chrono::steady_clock;
    using Second = std::chrono::duration<double, std::ratio<1>>;

    // m_beg 表示计时起点。
    std::chrono::time_point<Clock> m_beg{Clock::now()};

public:
    // reset 重新记录起点时间。
    void reset() {
        m_beg = Clock::now();
    }

    // elapsed 返回从起点到当前时刻经过的秒数。
    double elapsed() const {
        return std::chrono::duration_cast<Second>(Clock::now() - m_beg).count();
    }
};
