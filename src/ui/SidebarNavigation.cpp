#include "ui/SidebarNavigation.h"

#include "ui/MainWindow.h"

namespace
{
    constexpr UINT kIdNavProcesses = 1010;
    constexpr UINT kIdNavPerformance = 1011;
    constexpr UINT kIdNavNetwork = 1012;
    constexpr UINT kIdNavHardware = 1013;
    constexpr UINT kIdNavServices = 1014;
    constexpr UINT kIdNavStartupApps = 1015;
    constexpr UINT kIdNavUsers = 1016;
    constexpr UINT kIdNavQuickTools = 1017;
}

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
        case kIdNavProcesses:
            owner_->SetActiveSection(MainWindow::Section::Processes);
            return true;
        case kIdNavPerformance:
            owner_->SetActiveSection(MainWindow::Section::Performance);
            return true;
        case kIdNavNetwork:
            owner_->SetActiveSection(MainWindow::Section::Network);
            return true;
        case kIdNavHardware:
            owner_->SetActiveSection(MainWindow::Section::Hardware);
            return true;
        case kIdNavServices:
            owner_->SetActiveSection(MainWindow::Section::Services);
            return true;
        case kIdNavStartupApps:
            owner_->SetActiveSection(MainWindow::Section::StartupApps);
            return true;
        case kIdNavUsers:
            owner_->SetActiveSection(MainWindow::Section::Users);
            return true;
        case kIdNavQuickTools:
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

        int checked = kIdNavProcesses;
        switch (owner_->activeSection_)
        {
        case MainWindow::Section::Processes:
            checked = kIdNavProcesses;
            break;
        case MainWindow::Section::Performance:
            checked = kIdNavPerformance;
            break;
        case MainWindow::Section::Network:
            checked = kIdNavNetwork;
            break;
        case MainWindow::Section::Hardware:
            checked = kIdNavHardware;
            break;
        case MainWindow::Section::Services:
            checked = kIdNavServices;
            break;
        case MainWindow::Section::StartupApps:
            checked = kIdNavStartupApps;
            break;
        case MainWindow::Section::Users:
            checked = kIdNavUsers;
            break;
        case MainWindow::Section::QuickKillTools:
            checked = kIdNavQuickTools;
            break;
        }

        CheckRadioButton(owner_->hwnd_, kIdNavProcesses, kIdNavQuickTools, checked);
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
