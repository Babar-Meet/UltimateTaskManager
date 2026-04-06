#pragma once

#include <windows.h>

namespace utm::ui
{

    class MainWindow;

    class HardwarePanel
    {
    public:
        void Attach(MainWindow *owner);
        bool HandleCommand(UINT id, UINT code);
        bool HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result);

    private:
        MainWindow *owner_ = nullptr;
    };

} // namespace utm::ui
