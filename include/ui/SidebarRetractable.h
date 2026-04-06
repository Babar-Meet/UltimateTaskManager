#pragma once

#include <windows.h>

namespace utm::ui
{

    class MainWindow;

    class SidebarRetractable
    {
    public:
        struct Metrics
        {
            int sidebarWidth = 0;
            int contentGap = 18;
            int navInsetX = 0;
            int navStartY = 0;
            int navButtonHeight = 34;
            int navGap = 6;
            int quickToolsGap = 14;
            int toggleX = 0;
            int toggleY = 0;
            int toggleWidth = 0;
            int toggleHeight = 0;
            int titleX = 0;
            int titleY = 0;
            int titleWidth = 0;
            int titleHeight = 0;
            bool showTitle = true;
        };

        void Attach(MainWindow *owner);
        bool HandleCommand(UINT id);
        Metrics CurrentMetrics() const;
        bool IsCollapsed() const;
        bool IsSidebarToggleControl(HWND control) const;
        void ApplyMode() const;

    private:
        MainWindow *owner_ = nullptr;
        bool collapsed_ = false;
    };

} // namespace utm::ui
