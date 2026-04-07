#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace utm::system::startup
{

    enum class StartupScope
    {
        CurrentUser,
        AllUsers,
        SpecificUser
    };

    enum class StartupSource
    {
        RegistryRun,
        RegistryRun32,
        StartupFolder
    };

    struct StartupAppEntry
    {
        std::wstring id;
        std::wstring name;
        std::wstring command;
        std::wstring startupType;
        std::wstring scopeText;
        std::wstring ownerUser;
        std::wstring ownerSid;
        std::wstring sourceLocation;
        std::wstring statusText;

        bool enabled = true;
        bool canToggle = true;
        bool canOpenLocation = false;
        std::wstring locationPath;

        StartupScope scope = StartupScope::CurrentUser;
        StartupSource source = StartupSource::RegistryRun;
        std::wstring approvalValueName;
    };

    struct StartupUserTarget
    {
        std::wstring id;
        std::wstring displayName;
    };

    class StartupAppsManager
    {
    public:
        static bool EnumerateStartupUserTargets(std::vector<StartupUserTarget> &targets, std::wstring &errorText);
        static bool EnumerateStartupApps(std::vector<StartupAppEntry> &entries, std::wstring &errorText, const std::wstring &targetId = L"current-user");
        static bool SetStartupEnabled(const StartupAppEntry &entry, bool enable, std::wstring &errorText);
    };

} // namespace utm::system::startup
