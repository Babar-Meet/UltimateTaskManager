#include "system/startup/StartupAppsManager.h"

#include "system/startup/StartupApprovalStore.h"

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <sstream>
#include <vector>

namespace
{
    constexpr const wchar_t *kRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t *kRunPathWow6432 = L"Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";

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

    std::wstring RootToText(HKEY root)
    {
        return root == HKEY_CURRENT_USER ? L"HKCU" : L"HKLM";
    }

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

    std::wstring Trim(const std::wstring &value)
    {
        size_t start = 0;
        while (start < value.size() && iswspace(value[start]))
        {
            ++start;
        }

        size_t end = value.size();
        while (end > start && iswspace(value[end - 1]))
        {
            --end;
        }

        return value.substr(start, end - start);
    }

    std::wstring ExpandEnvironmentString(const std::wstring &text)
    {
        if (text.empty())
        {
            return text;
        }

        DWORD required = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
        if (required == 0)
        {
            return text;
        }

        std::wstring buffer(required, L'\0');
        const DWORD written = ExpandEnvironmentStringsW(text.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0)
        {
            return text;
        }

        if (!buffer.empty() && buffer.back() == L'\0')
        {
            buffer.pop_back();
        }

        return buffer;
    }

    std::wstring GuessExecutablePathFromCommand(const std::wstring &command)
    {
        const std::wstring expanded = ExpandEnvironmentString(Trim(command));
        if (expanded.empty())
        {
            return L"";
        }

        std::wstring candidate;
        if (!expanded.empty() && expanded.front() == L'"')
        {
            const size_t endQuote = expanded.find(L'"', 1);
            if (endQuote != std::wstring::npos && endQuote > 1)
            {
                candidate = expanded.substr(1, endQuote - 1);
            }
        }
        else
        {
            const size_t firstSpace = expanded.find(L' ');
            candidate = firstSpace == std::wstring::npos ? expanded : expanded.substr(0, firstSpace);
        }

        candidate = Trim(candidate);
        if (candidate.empty())
        {
            return L"";
        }

        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
        {
            return candidate;
        }

        wchar_t resolved[MAX_PATH]{};
        DWORD chars = SearchPathW(nullptr, candidate.c_str(), L".exe", static_cast<DWORD>(std::size(resolved)), resolved, nullptr);
        if (chars > 0 && chars < static_cast<DWORD>(std::size(resolved)))
        {
            return resolved;
        }

        return L"";
    }

    void EnumerateRunKey(
        HKEY root,
        const std::wstring &runPath,
        utm::system::startup::StartupSource source,
        std::vector<utm::system::startup::StartupAppEntry> &entries,
        std::wstring &errorText)
    {
        HKEY key = nullptr;
        const LONG openResult = RegOpenKeyExW(root, runPath.c_str(), 0, KEY_READ, &key);
        if (openResult == ERROR_FILE_NOT_FOUND)
        {
            return;
        }

        if (openResult != ERROR_SUCCESS)
        {
            if (errorText.empty())
            {
                errorText = L"RegOpenKeyEx failed for " + RootToText(root) + L"\\" + runPath + L". LastError=" + Win32ErrorToText(static_cast<DWORD>(openResult));
            }
            return;
        }

        ScopedRegistryKey scopedKey(key);

        DWORD valueCount = 0;
        DWORD maxNameLen = 0;
        DWORD maxDataLen = 0;
        const LONG queryInfoResult = RegQueryInfoKeyW(
            scopedKey.Get(),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &valueCount,
            &maxNameLen,
            &maxDataLen,
            nullptr,
            nullptr);

        if (queryInfoResult != ERROR_SUCCESS)
        {
            if (errorText.empty())
            {
                errorText = L"RegQueryInfoKey failed for startup entries. LastError=" + Win32ErrorToText(static_cast<DWORD>(queryInfoResult));
            }
            return;
        }

        std::vector<wchar_t> nameBuffer(maxNameLen + 2, L'\0');
        std::vector<BYTE> dataBuffer((std::max)(maxDataLen + 2, 1024UL), 0);

        for (DWORD index = 0; index < valueCount; ++index)
        {
            DWORD nameLen = static_cast<DWORD>(nameBuffer.size());
            DWORD dataLen = static_cast<DWORD>(dataBuffer.size());
            DWORD type = 0;

            const LONG enumResult = RegEnumValueW(
                scopedKey.Get(),
                index,
                nameBuffer.data(),
                &nameLen,
                nullptr,
                &type,
                dataBuffer.data(),
                &dataLen);

            if (enumResult != ERROR_SUCCESS)
            {
                continue;
            }

            if (type != REG_SZ && type != REG_EXPAND_SZ)
            {
                continue;
            }

            const std::wstring valueName(nameBuffer.data(), nameLen);
            if (valueName.empty())
            {
                continue;
            }

            const wchar_t *rawData = reinterpret_cast<const wchar_t *>(dataBuffer.data());
            const size_t rawLength = dataLen >= sizeof(wchar_t) ? (dataLen / sizeof(wchar_t)) - 1 : 0;
            const std::wstring rawCommand(rawData ? rawData : L"", rawLength);
            const std::wstring displayCommand = type == REG_EXPAND_SZ ? ExpandEnvironmentString(rawCommand) : rawCommand;

            const utm::system::startup::StartupScope scope =
                root == HKEY_CURRENT_USER
                    ? utm::system::startup::StartupScope::CurrentUser
                    : utm::system::startup::StartupScope::AllUsers;

            bool enabled = true;
            std::wstring approvalError;
            if (!utm::system::startup::detail::ReadStartupApproved(scope, source, valueName, enabled, approvalError))
            {
                enabled = true;
            }

            utm::system::startup::StartupAppEntry entry{};
            entry.scope = scope;
            entry.source = source;
            entry.name = valueName;
            entry.command = displayCommand;
            entry.startupType = utm::system::startup::detail::SourceToTypeText(source);
            entry.scopeText = utm::system::startup::detail::ScopeToText(scope);
            entry.sourceLocation = RootToText(root) + L"\\" + runPath;
            entry.enabled = enabled;
            entry.statusText = enabled ? L"Enabled" : L"Disabled";
            entry.approvalValueName = valueName;
            entry.id = entry.sourceLocation + L"|" + valueName;

            entry.locationPath = GuessExecutablePathFromCommand(displayCommand);
            entry.canOpenLocation = !entry.locationPath.empty();

            entries.push_back(std::move(entry));
        }
    }

    bool QueryKnownFolderPath(REFKNOWNFOLDERID folderId, std::wstring &folderPath)
    {
        folderPath.clear();

        PWSTR path = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path);
        if (FAILED(hr) || !path)
        {
            return false;
        }

        folderPath = path;
        CoTaskMemFree(path);
        return !folderPath.empty();
    }

    void EnumerateStartupFolder(
        const std::wstring &folderPath,
        utm::system::startup::StartupScope scope,
        std::vector<utm::system::startup::StartupAppEntry> &entries)
    {
        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec))
        {
            return;
        }

        for (const auto &item : std::filesystem::directory_iterator(folderPath, ec))
        {
            if (ec)
            {
                break;
            }

            if (!item.is_regular_file(ec))
            {
                continue;
            }

            const std::wstring fileName = item.path().filename().wstring();
            const std::wstring filePath = item.path().wstring();

            bool enabled = true;
            std::wstring approvalError;
            utm::system::startup::detail::ReadStartupApproved(
                scope,
                utm::system::startup::StartupSource::StartupFolder,
                fileName,
                enabled,
                approvalError);

            utm::system::startup::StartupAppEntry entry{};
            entry.scope = scope;
            entry.source = utm::system::startup::StartupSource::StartupFolder;
            entry.name = item.path().stem().wstring();
            entry.command = filePath;
            entry.startupType = utm::system::startup::detail::SourceToTypeText(entry.source);
            entry.scopeText = utm::system::startup::detail::ScopeToText(scope);
            entry.sourceLocation = folderPath;
            entry.enabled = enabled;
            entry.statusText = enabled ? L"Enabled" : L"Disabled";
            entry.approvalValueName = fileName;
            entry.id = folderPath + L"|" + fileName;
            entry.locationPath = filePath;
            entry.canOpenLocation = true;

            entries.push_back(std::move(entry));
        }
    }

} // namespace

namespace utm::system::startup
{

    bool StartupAppsManager::EnumerateStartupApps(std::vector<StartupAppEntry> &entries, std::wstring &errorText)
    {
        entries.clear();
        errorText.clear();

        EnumerateRunKey(HKEY_CURRENT_USER, kRunPath, StartupSource::RegistryRun, entries, errorText);
        EnumerateRunKey(HKEY_LOCAL_MACHINE, kRunPath, StartupSource::RegistryRun, entries, errorText);
        EnumerateRunKey(HKEY_LOCAL_MACHINE, kRunPathWow6432, StartupSource::RegistryRun32, entries, errorText);

        std::wstring userStartupPath;
        if (QueryKnownFolderPath(FOLDERID_Startup, userStartupPath))
        {
            EnumerateStartupFolder(userStartupPath, StartupScope::CurrentUser, entries);
        }

        std::wstring commonStartupPath;
        if (QueryKnownFolderPath(FOLDERID_CommonStartup, commonStartupPath))
        {
            EnumerateStartupFolder(commonStartupPath, StartupScope::AllUsers, entries);
        }

        std::sort(entries.begin(), entries.end(), [](const StartupAppEntry &left, const StartupAppEntry &right)
                  {
                      if (left.name == right.name)
                      {
                          return left.sourceLocation < right.sourceLocation;
                      }
                      return left.name < right.name; });

        return true;
    }

    bool StartupAppsManager::SetStartupEnabled(const StartupAppEntry &entry, bool enable, std::wstring &errorText)
    {
        errorText.clear();

        if (entry.approvalValueName.empty())
        {
            errorText = L"Startup entry does not include an approval identity, so it cannot be toggled.";
            return false;
        }

        return detail::WriteStartupApproved(entry.scope, entry.source, entry.approvalValueName, enable, errorText);
    }

} // namespace utm::system::startup
