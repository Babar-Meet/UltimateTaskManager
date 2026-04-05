#include "util/security/PrivilegeManager.h"

#include <windows.h>

namespace utm::util::security
{

    bool PrivilegeManager::IsProcessElevated()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            return false;
        }

        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);

        return ok == TRUE && elevation.TokenIsElevated != 0;
    }

    bool PrivilegeManager::EnablePrivilege(const wchar_t *privilegeName, std::wstring &detail)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        {
            detail = L"OpenProcessToken failed.";
            return false;
        }

        LUID luid{};
        if (!LookupPrivilegeValueW(nullptr, privilegeName, &luid))
        {
            detail = L"LookupPrivilegeValueW failed.";
            CloseHandle(token);
            return false;
        }

        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (!AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr))
        {
            detail = L"AdjustTokenPrivileges failed.";
            CloseHandle(token);
            return false;
        }

        const DWORD last = GetLastError();
        CloseHandle(token);

        if (last == ERROR_NOT_ALL_ASSIGNED)
        {
            detail = L"Privilege is not assigned to this token.";
            return false;
        }

        detail = L"Privilege enabled.";
        return true;
    }

    PrivilegeState PrivilegeManager::InitializePrivileges()
    {
        PrivilegeState state{};
        state.isElevated = IsProcessElevated();

        std::wstring detail;
        state.seDebugEnabled = EnablePrivilege(SE_DEBUG_NAME, detail);
        state.detail = detail;

        return state;
    }

} // namespace utm::util::security
