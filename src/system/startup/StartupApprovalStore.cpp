#include "system/startup/StartupApprovalStore.h"

#include <array>
#include <sstream>

namespace
{
    constexpr const wchar_t *kApprovedRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    constexpr const wchar_t *kApprovedRun32Path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32";
    constexpr const wchar_t *kApprovedStartupFolderPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";

    class ScopedRegistryKey
    {
    public:
        ScopedRegistryKey() = default;

        explicit ScopedRegistryKey(HKEY key) : key_(key)
        {
        }

        ScopedRegistryKey(const ScopedRegistryKey &) = delete;
        ScopedRegistryKey &operator=(const ScopedRegistryKey &) = delete;

        ~ScopedRegistryKey()
        {
            if (key_)
            {
                RegCloseKey(key_);
            }
        }

        HKEY Get() const
        {
            return key_;
        }

    private:
        HKEY key_ = nullptr;
    };

    std::wstring Win32ErrorToText(DWORD error)
    {
        if (error == 0)
        {
            return L"0";
        }

        wchar_t *buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = FormatMessageW(
            flags,
            nullptr,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        std::wstringstream out;
        out << error;

        if (length > 0 && buffer)
        {
            std::wstring message(buffer, length);
            while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
            {
                message.pop_back();
            }

            if (!message.empty())
            {
                out << L" (" << message << L")";
            }
        }

        if (buffer)
        {
            LocalFree(buffer);
        }

        return out.str();
    }

    std::wstring SourceToApprovalPath(utm::system::startup::StartupSource source)
    {
        switch (source)
        {
        case utm::system::startup::StartupSource::RegistryRun:
            return kApprovedRunPath;
        case utm::system::startup::StartupSource::RegistryRun32:
            return kApprovedRun32Path;
        case utm::system::startup::StartupSource::StartupFolder:
            return kApprovedStartupFolderPath;
        default:
            return kApprovedRunPath;
        }
    }

} // namespace

namespace utm::system::startup::detail
{

    HKEY ScopeToRootKey(StartupScope scope)
    {
        return scope == StartupScope::CurrentUser ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    }

    std::wstring ScopeToText(StartupScope scope)
    {
        return scope == StartupScope::CurrentUser ? L"Current User" : L"All Users";
    }

    std::wstring SourceToTypeText(StartupSource source)
    {
        switch (source)
        {
        case StartupSource::RegistryRun:
            return L"Registry (Run)";
        case StartupSource::RegistryRun32:
            return L"Registry (Run 32-bit)";
        case StartupSource::StartupFolder:
            return L"Startup Folder";
        default:
            return L"Unknown";
        }
    }

    bool ReadStartupApproved(
        StartupScope scope,
        StartupSource source,
        const std::wstring &valueName,
        bool &enabled,
        std::wstring &errorText)
    {
        errorText.clear();
        enabled = true;

        const HKEY root = ScopeToRootKey(scope);
        const std::wstring subKey = SourceToApprovalPath(source);

        HKEY key = nullptr;
        const LONG openResult = RegOpenKeyExW(root, subKey.c_str(), 0, KEY_QUERY_VALUE, &key);
        if (openResult == ERROR_FILE_NOT_FOUND)
        {
            return true;
        }

        if (openResult != ERROR_SUCCESS)
        {
            errorText = L"RegOpenKeyEx failed for startup approval key. LastError=" + Win32ErrorToText(static_cast<DWORD>(openResult));
            return false;
        }

        ScopedRegistryKey scopedKey(key);

        DWORD type = 0;
        BYTE data[16]{};
        DWORD dataSize = static_cast<DWORD>(sizeof(data));
        const LONG queryResult = RegQueryValueExW(
            scopedKey.Get(),
            valueName.c_str(),
            nullptr,
            &type,
            data,
            &dataSize);

        if (queryResult == ERROR_FILE_NOT_FOUND)
        {
            return true;
        }

        if (queryResult != ERROR_SUCCESS)
        {
            errorText = L"RegQueryValueEx failed for startup approval value. LastError=" + Win32ErrorToText(static_cast<DWORD>(queryResult));
            return false;
        }

        if (type != REG_BINARY || dataSize == 0)
        {
            enabled = true;
            return true;
        }

        const BYTE state = data[0];
        enabled = state == 0x02 || state == 0x06;
        return true;
    }

    bool WriteStartupApproved(
        StartupScope scope,
        StartupSource source,
        const std::wstring &valueName,
        bool enable,
        std::wstring &errorText)
    {
        errorText.clear();

        const HKEY root = ScopeToRootKey(scope);
        const std::wstring subKey = SourceToApprovalPath(source);

        HKEY key = nullptr;
        const LONG createResult = RegCreateKeyExW(
            root,
            subKey.c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr);

        if (createResult != ERROR_SUCCESS)
        {
            errorText = L"RegCreateKeyEx failed for startup approval key. LastError=" + Win32ErrorToText(static_cast<DWORD>(createResult));
            return false;
        }

        ScopedRegistryKey scopedKey(key);

        std::array<BYTE, 12> data{};
        data[0] = enable ? 0x02 : 0x03;
        if (!enable)
        {
            FILETIME now{};
            GetSystemTimeAsFileTime(&now);
            data[4] = static_cast<BYTE>(now.dwLowDateTime & 0xFF);
            data[5] = static_cast<BYTE>((now.dwLowDateTime >> 8) & 0xFF);
            data[6] = static_cast<BYTE>((now.dwLowDateTime >> 16) & 0xFF);
            data[7] = static_cast<BYTE>((now.dwLowDateTime >> 24) & 0xFF);
            data[8] = static_cast<BYTE>(now.dwHighDateTime & 0xFF);
            data[9] = static_cast<BYTE>((now.dwHighDateTime >> 8) & 0xFF);
            data[10] = static_cast<BYTE>((now.dwHighDateTime >> 16) & 0xFF);
            data[11] = static_cast<BYTE>((now.dwHighDateTime >> 24) & 0xFF);
        }

        const LONG setResult = RegSetValueExW(
            scopedKey.Get(),
            valueName.c_str(),
            0,
            REG_BINARY,
            data.data(),
            static_cast<DWORD>(data.size()));

        if (setResult != ERROR_SUCCESS)
        {
            errorText = L"RegSetValueEx failed while updating startup status. LastError=" + Win32ErrorToText(static_cast<DWORD>(setResult));
            return false;
        }

        return true;
    }

} // namespace utm::system::startup::detail
