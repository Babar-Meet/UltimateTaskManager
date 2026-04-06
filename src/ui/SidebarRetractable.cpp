#include "ui/SidebarRetractable.h"

#include "ui/MainWindow.h"
#include "ui/SidebarIds.h"

namespace
{
    constexpr int kExpandedSidebarWidth = 252;
    constexpr int kCollapsedSidebarWidth = 76;
}

namespace utm::ui
{

    void SidebarRetractable::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool SidebarRetractable::HandleCommand(UINT id)
    {
        if (!owner_ || id != sidebar_ids::kSidebarToggle)
        {
            return false;
        }

        collapsed_ = !collapsed_;
        ApplyMode();
        owner_->LayoutControls();
        return true;
    }

    SidebarRetractable::Metrics SidebarRetractable::CurrentMetrics() const
    {
        Metrics metrics{};
        if (collapsed_)
        {
            metrics.sidebarWidth = kCollapsedSidebarWidth;
            metrics.navInsetX = 8;
            metrics.navStartY = 58;
            metrics.quickToolsGap = 10;

            metrics.toggleWidth = 42;
            metrics.toggleHeight = 30;
            metrics.toggleX = (metrics.sidebarWidth - metrics.toggleWidth) / 2;
            metrics.toggleY = 14;
            metrics.showTitle = false;
            return metrics;
        }

        metrics.sidebarWidth = kExpandedSidebarWidth;
        metrics.navInsetX = 14;
        metrics.navStartY = 88;
        metrics.quickToolsGap = 14;

        metrics.toggleX = 14;
        metrics.toggleY = 12;
        metrics.toggleWidth = metrics.sidebarWidth - 28;
        metrics.toggleHeight = 30;

        metrics.titleX = 14;
        metrics.titleY = 48;
        metrics.titleWidth = metrics.sidebarWidth - 28;
        metrics.titleHeight = 30;
        metrics.showTitle = true;
        return metrics;
    }

    bool SidebarRetractable::IsCollapsed() const
    {
        return collapsed_;
    }

    bool SidebarRetractable::IsSidebarToggleControl(HWND control) const
    {
        return owner_ && control == owner_->sidebarToggle_;
    }

    void SidebarRetractable::ApplyMode() const
    {
        if (!owner_)
        {
            return;
        }

        SetWindowTextW(owner_->sidebarToggle_, collapsed_ ? L">>>" : L"<<<");

        if (collapsed_)
        {
            SetWindowTextW(owner_->sidebarTitle_, L"TM");
            SetWindowTextW(owner_->navProcesses_, L"PR");
            SetWindowTextW(owner_->navPerformance_, L"PF");
            SetWindowTextW(owner_->navNetwork_, L"NW");
            SetWindowTextW(owner_->navHardware_, L"HW");
            SetWindowTextW(owner_->navServices_, L"SV");
            SetWindowTextW(owner_->navStartupApps_, L"SU");
            SetWindowTextW(owner_->navUsers_, L"US");
            SetWindowTextW(owner_->navQuickTools_, L"QT");
            return;
        }

        SetWindowTextW(owner_->sidebarTitle_, L"Task Manager");
        SetWindowTextW(owner_->navProcesses_, L"Processes");
        SetWindowTextW(owner_->navPerformance_, L"Performance");
        SetWindowTextW(owner_->navNetwork_, L"Network");
        SetWindowTextW(owner_->navHardware_, L"Hardware");
        SetWindowTextW(owner_->navServices_, L"Services");
        SetWindowTextW(owner_->navStartupApps_, L"Startup Apps");
        SetWindowTextW(owner_->navUsers_, L"Users");
        SetWindowTextW(owner_->navQuickTools_, L"Quick Kill Tools");
    }

} // namespace utm::ui
