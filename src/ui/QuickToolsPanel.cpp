#include "ui/QuickToolsPanel.h"

#include "ui/MainWindow.h"

namespace
{
    constexpr UINT kIdQuickToolPortKillAll = 41000;
    constexpr UINT kIdQuickToolProcessKillAll = 41001;
    constexpr UINT kIdQuickToolSmartDelete = 41002;
    constexpr UINT kIdQuickToolPortKillOneByOne = 41003;
    constexpr UINT kIdQuickToolProcessKillOneByOne = 41004;
    constexpr UINT kIdQuickToolBrowseFile = 41005;
    constexpr UINT kIdQuickToolBrowseFolder = 41006;
}

namespace utm::ui
{

    void QuickToolsPanel::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool QuickToolsPanel::HandleCommand(UINT id)
    {
        if (!owner_)
        {
            return false;
        }

        if (id == kIdQuickToolPortKillAll ||
            id == kIdQuickToolPortKillOneByOne ||
            id == kIdQuickToolProcessKillAll ||
            id == kIdQuickToolProcessKillOneByOne ||
            id == kIdQuickToolSmartDelete ||
            id == kIdQuickToolBrowseFile ||
            id == kIdQuickToolBrowseFolder)
        {
            return owner_->HandleQuickToolsCommand(id);
        }

        return false;
    }

} // namespace utm::ui
