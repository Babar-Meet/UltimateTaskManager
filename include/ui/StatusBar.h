#pragma once

#include <windows.h>

#include <string>

namespace utm::ui
{

    class MainWindow;

    class StatusBar
    {
    public:
        void Attach(MainWindow *owner);
        void ShowText(const std::wstring &text);
        bool IsStatusControl(HWND control) const;

    private:
        MainWindow *owner_ = nullptr;
    };

} // namespace utm::ui
