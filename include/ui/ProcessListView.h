#pragma once

#include <windows.h>

namespace utm::ui
{

    class MainWindow;

    class ProcessListView
    {
    public:
        void Attach(MainWindow *owner);
        bool HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result);
        bool HandleCommand(UINT id, UINT code);

    private:
        MainWindow *owner_ = nullptr;
    };

} // namespace utm::ui
