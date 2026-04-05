#include "util/logging/Logger.h"

#include <windows.h>
#include <filesystem>
#include <chrono>
#include <iomanip>

namespace utm::util::logging
{

    Logger &Logger::Instance()
    {
        static Logger logger;
        return logger;
    }

    Logger::~Logger()
    {
        std::scoped_lock lock(mutex_);
        if (stream_.is_open())
        {
            stream_.flush();
            stream_.close();
        }
    }

    std::wstring Logger::BuildDefaultPath() const
    {
        wchar_t tempPath[MAX_PATH]{};
        const DWORD result = GetTempPathW(MAX_PATH, tempPath);
        std::filesystem::path base = result > 0 ? std::filesystem::path(tempPath) : std::filesystem::temp_directory_path();

        std::filesystem::path folder = base / L"UltimateTaskManager";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);

        return (folder / L"utm.log").wstring();
    }

    const wchar_t *Logger::LevelToString(LogLevel level) const
    {
        switch (level)
        {
        case LogLevel::Debug:
            return L"DEBUG";
        case LogLevel::Info:
            return L"INFO";
        case LogLevel::Warning:
            return L"WARN";
        case LogLevel::Error:
            return L"ERROR";
        default:
            return L"UNK";
        }
    }

    void Logger::Initialize(const std::wstring &filePath)
    {
        std::scoped_lock lock(mutex_);
        if (initialized_)
        {
            return;
        }

        const std::wstring finalPath = filePath.empty() ? BuildDefaultPath() : filePath;
        stream_.open(finalPath, std::ios::out | std::ios::app);
        initialized_ = stream_.is_open();

        if (initialized_)
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
            tm localTime{};
            localtime_s(&localTime, &nowTime);

            stream_ << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S")
                    << L" [INFO] Logger initialized.\n";
            stream_.flush();
        }
    }

    void Logger::Write(LogLevel level, std::wstring_view message)
    {
        std::scoped_lock lock(mutex_);
        if (!initialized_ || !stream_.is_open())
        {
            return;
        }

        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

        tm localTime{};
        localtime_s(&localTime, &nowTime);

        stream_ << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S")
                << L" [" << LevelToString(level) << L"] "
                << message << L"\n";

        stream_.flush();
    }

} // namespace utm::util::logging
