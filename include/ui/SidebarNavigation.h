#pragma once

#include <windows.h>

#include <string>

namespace utm::ui
{

    class MainWindow;

    class SidebarNavigation
    {
    public:
        void Attach(MainWindow *owner);
        bool HandleCommand(UINT id);
        void UpdateSelection() const;
        std::wstring ActiveSectionTitle() const;
        bool IsSidebarStaticControl(HWND control) const;
        bool IsSidebarButtonControl(HWND control) const;

    private:
        MainWindow *owner_ = nullptr;
    };

} // namespace utm::ui
