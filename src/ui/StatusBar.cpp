#include "ui/StatusBar.h"

#include "ui/MainWindow.h"

namespace utm::ui
{

    void StatusBar::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    void StatusBar::ShowText(const std::wstring &text)
    {
        if (owner_ && owner_->statusText_)
        {
            SetWindowTextW(owner_->statusText_, text.c_str());
        }
    }

    bool StatusBar::IsStatusControl(HWND control) const
    {
        return owner_ && control == owner_->statusText_;
    }

    void MainWindow::ShowStatusText(const std::wstring &text)
    {
        statusBarComponent_.ShowText(text);
    }

} // namespace utm::ui
