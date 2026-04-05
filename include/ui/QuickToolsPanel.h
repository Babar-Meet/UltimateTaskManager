#pragma once

#include <windows.h>

namespace utm::ui
{

    class MainWindow;

    class QuickToolsPanel
    {
    public:
        void Attach(MainWindow *owner);
        bool HandleCommand(UINT id);

    private:
        MainWindow *owner_ = nullptr;
    };

} // namespace utm::ui
