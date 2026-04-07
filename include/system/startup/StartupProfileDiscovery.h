#pragma once

#include <string>
#include <vector>

namespace utm::system::startup::detail
{

    struct StartupProfile
    {
        std::wstring sid;
        std::wstring accountName;
        std::wstring profilePath;
        bool isCurrentUser = false;
        bool hiveLoaded = false;
    };

    bool EnumerateStartupProfiles(std::vector<StartupProfile> &profiles, std::wstring &errorText);
    std::wstring BuildStartupFolderPath(const std::wstring &profilePath);

} // namespace utm::system::startup::detail
