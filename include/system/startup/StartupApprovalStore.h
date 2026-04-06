#pragma once

#include "system/startup/StartupAppsManager.h"

#include <string>

namespace utm::system::startup::detail
{

    HKEY ScopeToRootKey(StartupScope scope);
    std::wstring ScopeToText(StartupScope scope);
    std::wstring SourceToTypeText(StartupSource source);

    bool ReadStartupApproved(
        StartupScope scope,
        StartupSource source,
        const std::wstring &valueName,
        bool &enabled,
        std::wstring &errorText);

    bool WriteStartupApproved(
        StartupScope scope,
        StartupSource source,
        const std::wstring &valueName,
        bool enable,
        std::wstring &errorText);

} // namespace utm::system::startup::detail
