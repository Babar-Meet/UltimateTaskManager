#include "system/startup/StartupProfileDiscovery.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <vector>

namespace
{
    constexpr const wchar_t *kProfileListPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\ProfileList";

    std::wstring ToLowerText(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    std::wstring NormalizePath(std::wstring value)
    {
        std::replace(value.begin(), value.end(), L'/', L'\\');
        while (value.size() > 3 && !value.empty() && value.back() == L'\\')
        {
            value.pop_back();
        }

        return ToLowerText(value);
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

    bool QueryStringValue(HKEY key, const wchar_t *valueName, std::wstring &value)
    {
        value.clear();

        DWORD type = 0;
        DWORD dataSize = 0;
        LONG result = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &dataSize);
        if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || dataSize == 0)
        {
            return false;
        }

        std::vector<wchar_t> buffer((dataSize / sizeof(wchar_t)) + 1, L'\0');
        result = RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(buffer.data()),
            &dataSize);

        if (result != ERROR_SUCCESS)
        {
            return false;
        }

        value.assign(buffer.data());
        if (type == REG_EXPAND_SZ)
        {
            value = ExpandEnvironmentString(value);
        }

        return !value.empty();
    }

    bool IsInteractiveProfileSid(const std::wstring &sid)
    {
        if (sid == L"S-1-5-18" || sid == L"S-1-5-19" || sid == L"S-1-5-20")
        {
            return false;
        }

        const std::wstring lowered = ToLowerText(sid);
        return lowered.rfind(L"s-1-5-21-", 0) == 0 || lowered.rfind(L"s-1-12-1-", 0) == 0;
    }

    bool IsHiveLoaded(const std::wstring &sid)
    {
        HKEY key = nullptr;
        const LONG openResult = RegOpenKeyExW(HKEY_USERS, sid.c_str(), 0, KEY_READ, &key);
        if (openResult != ERROR_SUCCESS)
        {
            return false;
        }

        RegCloseKey(key);
        return true;
    }

    std::wstring CurrentUserProfilePath()
    {
        const DWORD required = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
        if (required == 0)
        {
            return L"";
        }

        std::wstring buffer(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(L"USERPROFILE", buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0)
        {
            return L"";
        }

        if (!buffer.empty() && buffer.back() == L'\0')
        {
            buffer.pop_back();
        }

        return buffer;
    }

    std::wstring AccountNameFromProfilePath(const std::wstring &profilePath)
    {
        const std::filesystem::path path(profilePath);
        const std::wstring name = path.filename().wstring();
        if (!name.empty())
        {
            return name;
        }

        return profilePath;
    }

} // namespace

namespace utm::system::startup::detail
{

    bool EnumerateStartupProfiles(std::vector<StartupProfile> &profiles, std::wstring &errorText)
    {
        profiles.clear();
        errorText.clear();

        const std::wstring currentProfile = NormalizePath(CurrentUserProfilePath());

        HKEY profileListKey = nullptr;
        const LONG openResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, kProfileListPath, 0, KEY_READ, &profileListKey);
        if (openResult != ERROR_SUCCESS)
        {
            errorText = L"Unable to read Windows profile list for startup-user discovery.";
            return false;
        }

        DWORD subKeyCount = 0;
        DWORD maxSubKeyLen = 0;
        const LONG infoResult = RegQueryInfoKeyW(
            profileListKey,
            nullptr,
            nullptr,
            nullptr,
            &subKeyCount,
            &maxSubKeyLen,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);

        if (infoResult != ERROR_SUCCESS)
        {
            RegCloseKey(profileListKey);
            errorText = L"Unable to inspect Windows profile list metadata.";
            return false;
        }

        std::vector<wchar_t> sidBuffer(maxSubKeyLen + 2, L'\0');

        for (DWORD index = 0; index < subKeyCount; ++index)
        {
            DWORD sidLength = static_cast<DWORD>(sidBuffer.size());
            const LONG enumResult = RegEnumKeyExW(
                profileListKey,
                index,
                sidBuffer.data(),
                &sidLength,
                nullptr,
                nullptr,
                nullptr,
                nullptr);

            if (enumResult != ERROR_SUCCESS)
            {
                continue;
            }

            const std::wstring sid(sidBuffer.data(), sidLength);
            if (!IsInteractiveProfileSid(sid))
            {
                continue;
            }

            HKEY sidKey = nullptr;
            const LONG openSidResult = RegOpenKeyExW(profileListKey, sid.c_str(), 0, KEY_READ, &sidKey);
            if (openSidResult != ERROR_SUCCESS)
            {
                continue;
            }

            std::wstring profilePath;
            const bool hasProfilePath = QueryStringValue(sidKey, L"ProfileImagePath", profilePath);
            RegCloseKey(sidKey);

            if (!hasProfilePath)
            {
                continue;
            }

            StartupProfile profile{};
            profile.sid = sid;
            profile.profilePath = profilePath;
            profile.accountName = AccountNameFromProfilePath(profilePath);
            profile.hiveLoaded = IsHiveLoaded(profile.sid);

            const std::wstring normalized = NormalizePath(profile.profilePath);
            profile.isCurrentUser = !currentProfile.empty() && normalized == currentProfile;

            profiles.push_back(std::move(profile));
        }

        RegCloseKey(profileListKey);

        std::sort(profiles.begin(), profiles.end(), [](const StartupProfile &left, const StartupProfile &right)
                  {
                      if (left.isCurrentUser != right.isCurrentUser)
                      {
                          return left.isCurrentUser;
                      }

                      const std::wstring leftName = ToLowerText(left.accountName);
                      const std::wstring rightName = ToLowerText(right.accountName);
                      if (leftName == rightName)
                      {
                          return left.sid < right.sid;
                      }

                      return leftName < rightName;
                  });

        return true;
    }

    std::wstring BuildStartupFolderPath(const std::wstring &profilePath)
    {
        if (profilePath.empty())
        {
            return L"";
        }

        std::filesystem::path folder(profilePath);
        folder /= L"AppData";
        folder /= L"Roaming";
        folder /= L"Microsoft";
        folder /= L"Windows";
        folder /= L"Start Menu";
        folder /= L"Programs";
        folder /= L"Startup";
        return folder.wstring();
    }

} // namespace utm::system::startup::detail
