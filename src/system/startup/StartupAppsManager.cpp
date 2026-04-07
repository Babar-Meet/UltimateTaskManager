#include "system/startup/StartupAppsManager.h"

#include "system/startup/StartupApprovalStore.h"
#include "system/startup/StartupProfileDiscovery.h"

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <vector>

namespace
{
    constexpr const wchar_t *kRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr const wchar_t *kRunPathWow6432 = L"Software\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";

    constexpr const wchar_t *kApprovedRunPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    constexpr const wchar_t *kApprovedRun32Path = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32";
    constexpr const wchar_t *kApprovedStartupFolderPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";

    constexpr const wchar_t *kTargetCurrentUser = L"current-user";
    constexpr const wchar_t *kTargetSystemWide = L"system";
    constexpr const wchar_t *kTargetAllUsers = L"all-users";
    constexpr const wchar_t *kTargetSidPrefix = L"sid:";

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

    struct StartupEntryContext
    {
        utm::system::startup::StartupScope scope = utm::system::startup::StartupScope::CurrentUser;
        std::wstring scopeText;
        std::wstring userName;
        std::wstring userSid;
        bool canToggle = true;
        bool suppressOpenError = false;
    };

    enum class StartupTargetKind
    {
        CurrentUser,
        SystemWide,
        AllUsers,
        SpecificSid
    };

    struct ParsedStartupTarget
    {
        StartupTargetKind kind = StartupTargetKind::CurrentUser;
        std::wstring sid;
    };

    std::wstring ToLowerText(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    std::wstring RootToText(HKEY root)
    {
        if (root == HKEY_CURRENT_USER)
        {
            return L"HKCU";
        }

        if (root == HKEY_LOCAL_MACHINE)
        {
            return L"HKLM";
        }

        if (root == HKEY_USERS)
        {
            return L"HKU";
        }

        return L"Registry";
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

    bool ReadStartupApprovedForSpecificUser(
        const std::wstring &sid,
        utm::system::startup::StartupSource source,
        const std::wstring &valueName,
        bool &enabled)
    {
        enabled = true;
        if (sid.empty() || valueName.empty())
        {
            return false;
        }

        const std::wstring approvalPath = sid + L"\\" + SourceToApprovalPath(source);

        HKEY key = nullptr;
        const LONG openResult = RegOpenKeyExW(HKEY_USERS, approvalPath.c_str(), 0, KEY_QUERY_VALUE, &key);
        if (openResult == ERROR_FILE_NOT_FOUND || openResult == ERROR_ACCESS_DENIED)
        {
            return false;
        }

        if (openResult != ERROR_SUCCESS)
        {
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

        if (queryResult != ERROR_SUCCESS || type != REG_BINARY || dataSize == 0)
        {
            return false;
        }

        const BYTE state = data[0];
        enabled = state == 0x02 || state == 0x06;
        return true;
    }

    std::wstring ResolveCurrentUserName()
    {
        wchar_t buffer[256]{};
        DWORD size = static_cast<DWORD>(std::size(buffer));
        if (GetUserNameW(buffer, &size) && size > 1)
        {
            return std::wstring(buffer, size - 1);
        }

        return L"Current User";
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

    void EnumerateRunKey(
        HKEY root,
        const std::wstring &runPath,
        utm::system::startup::StartupSource source,
        const StartupEntryContext &context,
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
            if (!context.suppressOpenError && errorText.empty())
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
            if (!context.suppressOpenError && errorText.empty())
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
            size_t charLength = dataLen / sizeof(wchar_t);
            if (charLength > 0 && rawData[charLength - 1] == L'\0')
            {
                --charLength;
            }

            const std::wstring rawCommand(rawData ? rawData : L"", charLength);
            const std::wstring displayCommand = type == REG_EXPAND_SZ ? ExpandEnvironmentString(rawCommand) : rawCommand;

            bool enabled = true;
            if (context.scope == utm::system::startup::StartupScope::SpecificUser)
            {
                ReadStartupApprovedForSpecificUser(context.userSid, source, valueName, enabled);
            }
            else
            {
                std::wstring approvalError;
                if (!utm::system::startup::detail::ReadStartupApproved(context.scope, source, valueName, enabled, approvalError))
                {
                    enabled = true;
                }
            }

            utm::system::startup::StartupAppEntry entry{};
            entry.scope = context.scope;
            entry.source = source;
            entry.name = valueName;
            entry.command = displayCommand;
            entry.startupType = utm::system::startup::detail::SourceToTypeText(source);
            entry.scopeText = context.scopeText.empty() ? utm::system::startup::detail::ScopeToText(context.scope) : context.scopeText;
            entry.ownerUser = context.userName;
            entry.ownerSid = context.userSid;
            entry.sourceLocation = RootToText(root) + L"\\" + runPath;
            entry.enabled = enabled;
            entry.statusText = enabled ? L"Enabled" : L"Disabled";
            entry.approvalValueName = valueName;
            entry.canToggle = context.canToggle;
            entry.id = entry.sourceLocation + L"|" + valueName;
            if (!context.userSid.empty())
            {
                entry.id += L"|" + context.userSid;
            }

            entry.locationPath = GuessExecutablePathFromCommand(displayCommand);
            entry.canOpenLocation = !entry.locationPath.empty();

            entries.push_back(std::move(entry));
        }
    }

    void EnumerateStartupFolder(
        const std::wstring &folderPath,
        const StartupEntryContext &context,
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
            if (context.scope == utm::system::startup::StartupScope::SpecificUser)
            {
                ReadStartupApprovedForSpecificUser(context.userSid, utm::system::startup::StartupSource::StartupFolder, fileName, enabled);
            }
            else
            {
                std::wstring approvalError;
                utm::system::startup::detail::ReadStartupApproved(
                    context.scope,
                    utm::system::startup::StartupSource::StartupFolder,
                    fileName,
                    enabled,
                    approvalError);
            }

            utm::system::startup::StartupAppEntry entry{};
            entry.scope = context.scope;
            entry.source = utm::system::startup::StartupSource::StartupFolder;
            entry.name = item.path().stem().wstring();
            entry.command = filePath;
            entry.startupType = utm::system::startup::detail::SourceToTypeText(entry.source);
            entry.scopeText = context.scopeText.empty() ? utm::system::startup::detail::ScopeToText(context.scope) : context.scopeText;
            entry.ownerUser = context.userName;
            entry.ownerSid = context.userSid;
            entry.sourceLocation = folderPath;
            entry.enabled = enabled;
            entry.statusText = enabled ? L"Enabled" : L"Disabled";
            entry.approvalValueName = fileName;
            entry.id = folderPath + L"|" + fileName;
            if (!context.userSid.empty())
            {
                entry.id += L"|" + context.userSid;
            }
            entry.locationPath = filePath;
            entry.canOpenLocation = true;
            entry.canToggle = context.canToggle;

            entries.push_back(std::move(entry));
        }
    }

    void AppendCurrentUserEntries(std::vector<utm::system::startup::StartupAppEntry> &entries, std::wstring &errorText)
    {
        StartupEntryContext context{};
        context.scope = utm::system::startup::StartupScope::CurrentUser;
        context.scopeText = L"Current User";
        context.userName = ResolveCurrentUserName();
        context.canToggle = true;

        EnumerateRunKey(HKEY_CURRENT_USER, kRunPath, utm::system::startup::StartupSource::RegistryRun, context, entries, errorText);

        std::wstring userStartupPath;
        if (!QueryKnownFolderPath(FOLDERID_Startup, userStartupPath))
        {
            userStartupPath = ExpandEnvironmentString(L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup");
        }

        if (!userStartupPath.empty())
        {
            EnumerateStartupFolder(userStartupPath, context, entries);
        }
    }

    void AppendSystemEntries(std::vector<utm::system::startup::StartupAppEntry> &entries, std::wstring &errorText)
    {
        StartupEntryContext context{};
        context.scope = utm::system::startup::StartupScope::AllUsers;
        context.scopeText = L"All Users (System)";
        context.userName = L"System";
        context.canToggle = true;

        EnumerateRunKey(HKEY_LOCAL_MACHINE, kRunPath, utm::system::startup::StartupSource::RegistryRun, context, entries, errorText);
        EnumerateRunKey(HKEY_LOCAL_MACHINE, kRunPathWow6432, utm::system::startup::StartupSource::RegistryRun32, context, entries, errorText);

        std::wstring commonStartupPath;
        if (!QueryKnownFolderPath(FOLDERID_CommonStartup, commonStartupPath))
        {
            commonStartupPath = ExpandEnvironmentString(L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup");
        }

        if (!commonStartupPath.empty())
        {
            EnumerateStartupFolder(commonStartupPath, context, entries);
        }
    }

    void AppendSpecificUserEntries(
        const utm::system::startup::detail::StartupProfile &profile,
        std::vector<utm::system::startup::StartupAppEntry> &entries,
        std::wstring &errorText)
    {
        if (profile.isCurrentUser)
        {
            AppendCurrentUserEntries(entries, errorText);
            return;
        }

        StartupEntryContext context{};
        context.scope = utm::system::startup::StartupScope::SpecificUser;
        context.scopeText = L"User: " + profile.accountName;
        context.userName = profile.accountName;
        context.userSid = profile.sid;
        context.canToggle = false;
        context.suppressOpenError = true;

        if (profile.hiveLoaded && !profile.sid.empty())
        {
            const std::wstring runPath = profile.sid + L"\\" + kRunPath;
            EnumerateRunKey(HKEY_USERS, runPath, utm::system::startup::StartupSource::RegistryRun, context, entries, errorText);
        }

        const std::wstring startupFolderPath = utm::system::startup::detail::BuildStartupFolderPath(profile.profilePath);
        if (!startupFolderPath.empty())
        {
            EnumerateStartupFolder(startupFolderPath, context, entries);
        }
    }

    ParsedStartupTarget ParseTargetId(const std::wstring &targetId)
    {
        ParsedStartupTarget parsed{};

        const std::wstring lowered = ToLowerText(targetId);
        if (lowered.empty() || lowered == kTargetCurrentUser)
        {
            parsed.kind = StartupTargetKind::CurrentUser;
            return parsed;
        }

        if (lowered == kTargetSystemWide)
        {
            parsed.kind = StartupTargetKind::SystemWide;
            return parsed;
        }

        if (lowered == kTargetAllUsers)
        {
            parsed.kind = StartupTargetKind::AllUsers;
            return parsed;
        }

        if (lowered.rfind(kTargetSidPrefix, 0) == 0)
        {
            parsed.kind = StartupTargetKind::SpecificSid;
            parsed.sid = targetId.substr(std::wcslen(kTargetSidPrefix));
            return parsed;
        }

        parsed.kind = StartupTargetKind::CurrentUser;
        return parsed;
    }

    const utm::system::startup::detail::StartupProfile *FindProfileBySid(
        const std::vector<utm::system::startup::detail::StartupProfile> &profiles,
        const std::wstring &sid)
    {
        const std::wstring loweredSid = ToLowerText(sid);
        for (const auto &profile : profiles)
        {
            if (ToLowerText(profile.sid) == loweredSid)
            {
                return &profile;
            }
        }

        return nullptr;
    }

    void SortEntries(std::vector<utm::system::startup::StartupAppEntry> &entries)
    {
        std::sort(entries.begin(), entries.end(), [](const utm::system::startup::StartupAppEntry &left, const utm::system::startup::StartupAppEntry &right)
                  {
                      const std::wstring leftOwner = left.ownerUser.empty() ? left.scopeText : left.ownerUser;
                      const std::wstring rightOwner = right.ownerUser.empty() ? right.scopeText : right.ownerUser;

                      if (leftOwner == rightOwner)
                      {
                          if (left.name == right.name)
                          {
                              return left.sourceLocation < right.sourceLocation;
                          }

                          return left.name < right.name;
                      }

                      return leftOwner < rightOwner; });
    }

} // namespace

namespace utm::system::startup
{

    bool StartupAppsManager::EnumerateStartupUserTargets(std::vector<StartupUserTarget> &targets, std::wstring &errorText)
    {
        targets.clear();
        errorText.clear();

        std::vector<detail::StartupProfile> profiles;
        std::wstring profileError;
        detail::EnumerateStartupProfiles(profiles, profileError);

        std::wstring currentUserLabel = L"Current User";
        for (const auto &profile : profiles)
        {
            if (profile.isCurrentUser && !profile.accountName.empty())
            {
                currentUserLabel += L" (" + profile.accountName + L")";
                break;
            }
        }

        targets.push_back({kTargetCurrentUser, currentUserLabel});
        targets.push_back({kTargetSystemWide, L"System Startup (All Users)"});
        targets.push_back({kTargetAllUsers, L"All Users + System"});

        for (const auto &profile : profiles)
        {
            if (profile.isCurrentUser || profile.sid.empty())
            {
                continue;
            }

            StartupUserTarget target{};
            target.id = std::wstring(kTargetSidPrefix) + profile.sid;
            target.displayName = profile.accountName.empty() ? profile.sid : profile.accountName;
            if (!profile.hiveLoaded)
            {
                target.displayName += L" (folder only)";
            }

            targets.push_back(std::move(target));
        }

        errorText = profileError;
        return true;
    }

    bool StartupAppsManager::EnumerateStartupApps(std::vector<StartupAppEntry> &entries, std::wstring &errorText, const std::wstring &targetId)
    {
        entries.clear();
        errorText.clear();

        const ParsedStartupTarget target = ParseTargetId(targetId);

        if (target.kind == StartupTargetKind::CurrentUser)
        {
            AppendCurrentUserEntries(entries, errorText);
        }
        else if (target.kind == StartupTargetKind::SystemWide)
        {
            AppendSystemEntries(entries, errorText);
        }
        else
        {
            std::vector<detail::StartupProfile> profiles;
            std::wstring profileError;
            detail::EnumerateStartupProfiles(profiles, profileError);

            if (target.kind == StartupTargetKind::AllUsers)
            {
                AppendCurrentUserEntries(entries, errorText);
                AppendSystemEntries(entries, errorText);

                for (const auto &profile : profiles)
                {
                    if (!profile.isCurrentUser)
                    {
                        AppendSpecificUserEntries(profile, entries, errorText);
                    }
                }
            }
            else if (target.kind == StartupTargetKind::SpecificSid)
            {
                const detail::StartupProfile *profile = FindProfileBySid(profiles, target.sid);
                if (profile)
                {
                    AppendSpecificUserEntries(*profile, entries, errorText);
                }
                else if (!target.sid.empty())
                {
                    StartupEntryContext context{};
                    context.scope = StartupScope::SpecificUser;
                    context.scopeText = L"User: " + target.sid;
                    context.userName = target.sid;
                    context.userSid = target.sid;
                    context.canToggle = false;
                    context.suppressOpenError = true;

                    const std::wstring runPath = target.sid + L"\\" + kRunPath;
                    EnumerateRunKey(HKEY_USERS, runPath, StartupSource::RegistryRun, context, entries, errorText);
                }
            }
        }

        SortEntries(entries);
        return true;
    }

    bool StartupAppsManager::SetStartupEnabled(const StartupAppEntry &entry, bool enable, std::wstring &errorText)
    {
        errorText.clear();

        if (!entry.canToggle || entry.scope == StartupScope::SpecificUser)
        {
            errorText = L"This startup entry belongs to another user profile and cannot be toggled from the current session.";
            return false;
        }

        if (entry.approvalValueName.empty())
        {
            errorText = L"Startup entry does not include an approval identity, so it cannot be toggled.";
            return false;
        }

        return detail::WriteStartupApproved(entry.scope, entry.source, entry.approvalValueName, enable, errorText);
    }

} // namespace utm::system::startup
