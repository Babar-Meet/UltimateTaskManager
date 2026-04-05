#pragma once

#include <string>

namespace utm::util::security
{

    struct PrivilegeState
    {
        bool isElevated = false;
        bool seDebugEnabled = false;
        std::wstring detail;
    };

    class PrivilegeManager
    {
    public:
        static PrivilegeState InitializePrivileges();
        static bool IsProcessElevated();

    private:
        static bool EnablePrivilege(const wchar_t *privilegeName, std::wstring &detail);
    };

} // namespace utm::util::security
