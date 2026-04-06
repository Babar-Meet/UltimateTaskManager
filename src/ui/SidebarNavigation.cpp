#include "ui/SidebarNavigation.h"

#include "ui/MainWindow.h"
#include "ui/SidebarIds.h"

namespace utm::ui
{

    void SidebarNavigation::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool SidebarNavigation::HandleCommand(UINT id)
    {
        if (!owner_)
        {
            return false;
        }

        switch (id)
        {
        case sidebar_ids::kNavProcesses:
            owner_->SetActiveSection(MainWindow::Section::Processes);
            return true;
        case sidebar_ids::kNavPerformance:
            owner_->SetActiveSection(MainWindow::Section::Performance);
            return true;
        case sidebar_ids::kNavNetwork:
            owner_->SetActiveSection(MainWindow::Section::Network);
            return true;
        case sidebar_ids::kNavHardware:
            owner_->SetActiveSection(MainWindow::Section::Hardware);
            return true;
        case sidebar_ids::kNavServices:
            owner_->SetActiveSection(MainWindow::Section::Services);
            return true;
        case sidebar_ids::kNavStartupApps:
            owner_->SetActiveSection(MainWindow::Section::StartupApps);
            return true;
        case sidebar_ids::kNavUsers:
            owner_->SetActiveSection(MainWindow::Section::Users);
            return true;
        case sidebar_ids::kNavQuickTools:
            owner_->SetActiveSection(MainWindow::Section::QuickKillTools);
            return true;
        default:
            return false;
        }
    }

    void SidebarNavigation::UpdateSelection() const
    {
        if (!owner_)
        {
            return;
        }

        int checked = static_cast<int>(sidebar_ids::kNavProcesses);
        switch (owner_->activeSection_)
        {
        case MainWindow::Section::Processes:
            checked = static_cast<int>(sidebar_ids::kNavProcesses);
            break;
        case MainWindow::Section::Performance:
            checked = static_cast<int>(sidebar_ids::kNavPerformance);
            break;
        case MainWindow::Section::Network:
            checked = static_cast<int>(sidebar_ids::kNavNetwork);
            break;
        case MainWindow::Section::Hardware:
            checked = static_cast<int>(sidebar_ids::kNavHardware);
            break;
        case MainWindow::Section::Services:
            checked = static_cast<int>(sidebar_ids::kNavServices);
            break;
        case MainWindow::Section::StartupApps:
            checked = static_cast<int>(sidebar_ids::kNavStartupApps);
            break;
        case MainWindow::Section::Users:
            checked = static_cast<int>(sidebar_ids::kNavUsers);
            break;
        case MainWindow::Section::QuickKillTools:
            checked = static_cast<int>(sidebar_ids::kNavQuickTools);
            break;
        }

        CheckRadioButton(
            owner_->hwnd_,
            static_cast<int>(sidebar_ids::kNavProcesses),
            static_cast<int>(sidebar_ids::kNavQuickTools),
            checked);
    }

    std::wstring SidebarNavigation::ActiveSectionTitle() const
    {
        if (!owner_)
        {
            return L"Ultimate Task Manager";
        }

        switch (owner_->activeSection_)
        {
        case MainWindow::Section::Processes:
            return L"Processes";
        case MainWindow::Section::Performance:
            return L"Performance";
        case MainWindow::Section::Network:
            return L"Network";
        case MainWindow::Section::Hardware:
            return L"Hardware";
        case MainWindow::Section::Services:
            return L"Services";
        case MainWindow::Section::StartupApps:
            return L"Startup Apps";
        case MainWindow::Section::Users:
            return L"Users";
        case MainWindow::Section::QuickKillTools:
            return L"Quick Kill Tools";
        default:
            return L"Ultimate Task Manager";
        }
    }

    bool SidebarNavigation::IsSidebarStaticControl(HWND control) const
    {
        return owner_ && (control == owner_->sidebar_ || GetParent(control) == owner_->sidebar_);
    }

    bool SidebarNavigation::IsSidebarButtonControl(HWND control) const
    {
        if (!owner_)
        {
            return false;
        }

        return control == owner_->navProcesses_ ||
               control == owner_->navPerformance_ ||
               control == owner_->navNetwork_ ||
               control == owner_->navHardware_ ||
               control == owner_->navServices_ ||
               control == owner_->navStartupApps_ ||
               control == owner_->navUsers_ ||
               control == owner_->navQuickTools_;
    }

} // namespace utm::ui
