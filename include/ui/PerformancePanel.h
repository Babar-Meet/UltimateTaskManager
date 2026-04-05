#pragma once

#include <windows.h>

namespace utm::ui
{

    class MainWindow;

    class PerformancePanel
    {
    public:
        void Attach(MainWindow *owner);
        bool HandleCommand(UINT id);
        bool HandleMouseWheel(WPARAM wParam, LPARAM lParam);
        bool HandleDrawItem(const DRAWITEMSTRUCT *draw, LRESULT &result) const;
        bool IsPerformanceButtonControl(HWND control) const;

    private:
        MainWindow *owner_ = nullptr;
    };

} // namespace utm::ui
