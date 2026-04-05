#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <fstream>

namespace utm::util::logging
{

    enum class LogLevel
    {
        Debug,
        Info,
        Warning,
        Error
    };

    class Logger
    {
    public:
        static Logger &Instance();

        void Initialize(const std::wstring &filePath = L"");
        void Write(LogLevel level, std::wstring_view message);

    private:
        Logger() = default;
        ~Logger();

        std::wstring BuildDefaultPath() const;
        const wchar_t *LevelToString(LogLevel level) const;

        std::wofstream stream_;
        std::mutex mutex_;
        bool initialized_ = false;
    };

} // namespace utm::util::logging
