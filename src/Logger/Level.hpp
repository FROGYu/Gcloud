#pragma once

// LogLevel 统一定义日志级别。
// 日志系统内部使用枚举，真正输出时再转成字符串，避免代码里散落字符串字面量。
class LogLevel {
public:
    // value 表示一条日志的严重程度。
    enum value {
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL
    };

    // ToString 用于格式化输出阶段，把枚举值转换成可读字符串。
    static const char* ToString(value level) {
        switch (level) {
            case DEBUG:
                return "DEBUG";
            case INFO:
                return "INFO";
            case WARN:
                return "WARN";
            case ERROR:
                return "ERROR";
            case FATAL:
                return "FATAL";
            default:
                return "UNKNOWN";
        }
    }
};
