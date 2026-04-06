#include "ui/ServicesPanel.h"

#include "ui/MainWindow.h"

#include "system/services/ServiceManager.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace
{
    constexpr UINT kIdServicesTitle = 1070;
    constexpr UINT kIdServicesHint = 1071;
    constexpr UINT kIdServicesSearchLabel = 1072;
    constexpr UINT kIdServicesSearchEdit = 1073;
    constexpr UINT kIdServicesModeLabel = 1074;
    constexpr UINT kIdServicesModeCombo = 1075;
    constexpr UINT kIdServicesList = 1076;
    constexpr UINT kIdServicesRefreshButton = 1077;
    constexpr UINT kIdServicesStartButton = 1078;
    constexpr UINT kIdServicesStopButton = 1079;
    constexpr UINT kIdServicesRestartButton = 1080;
    constexpr UINT kIdServicesOpenConsoleButton = 1081;

    std::wstring ToLowerServices(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitiveServices(const std::wstring &text, const std::wstring &loweredQuery)
    {
        if (loweredQuery.empty())
        {
            return true;
        }

        return ToLowerServices(text).find(loweredQuery) != std::wstring::npos;
    }

    bool IsServiceActiveState(DWORD state)
    {
        return state != SERVICE_STOPPED;
    }

    bool IsServicePendingState(DWORD state)
    {
        return state == SERVICE_START_PENDING ||
               state == SERVICE_STOP_PENDING ||
               state == SERVICE_CONTINUE_PENDING ||
               state == SERVICE_PAUSE_PENDING;
    }

} // namespace

namespace utm::ui
{

    void ServicesPanel::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool ServicesPanel::HandleCommand(UINT id, UINT code)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleServicesCommand(id, code);
    }

    bool ServicesPanel::HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleServicesNotify(hdr, lParam, result);
    }

    int MainWindow::SelectedServiceIndex() const
    {
        if (!servicesList_)
        {
            return -1;
        }

        const int selectedRow = ListView_GetNextItem(servicesList_, -1, LVNI_SELECTED);
        if (selectedRow < 0 || static_cast<size_t>(selectedRow) >= servicesVisibleRows_.size())
        {
            return -1;
        }

        const size_t serviceIndex = servicesVisibleRows_[static_cast<size_t>(selectedRow)];
        if (serviceIndex >= services_.size())
        {
            return -1;
        }

        return static_cast<int>(serviceIndex);
    }

    bool MainWindow::HandleServicesNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!hdr || hdr->hwndFrom != servicesList_)
        {
            return false;
        }

        if (hdr->code == LVN_ITEMCHANGED)
        {
            const auto *change = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if (change && (change->uChanged & LVIF_STATE) != 0)
            {
                RefreshServicesActionState();
                result = 0;
                return true;
            }
        }

        return false;
    }

    bool MainWindow::HandleServicesCommand(UINT id, UINT code)
    {
        if (id == kIdServicesSearchEdit && code == EN_CHANGE)
        {
            wchar_t buffer[320]{};
            GetWindowTextW(servicesSearchEdit_, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
            servicesFilterText_ = buffer;
            ApplyServicesFilterToList(true);
            return true;
        }

        if (id == kIdServicesModeCombo && code == CBN_SELCHANGE)
        {
            const int selected = static_cast<int>(SendMessageW(servicesModeCombo_, CB_GETCURSEL, 0, 0));
            switch (selected)
            {
            case 1:
                activeServiceFilterMode_ = ServiceFilterMode::Running;
                break;
            case 2:
                activeServiceFilterMode_ = ServiceFilterMode::Stopped;
                break;
            default:
                activeServiceFilterMode_ = ServiceFilterMode::All;
                break;
            }

            ApplyServicesFilterToList(true);
            return true;
        }

        if (id == kIdServicesRefreshButton)
        {
            RefreshServicesInventory(true);
            return true;
        }

        if (id == kIdServicesOpenConsoleButton)
        {
            const HINSTANCE openResult = ShellExecuteW(hwnd_, L"open", L"services.msc", nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(openResult) <= 32)
            {
                MessageBoxW(
                    hwnd_,
                    L"Unable to open Services console (services.msc).",
                    L"Services",
                    MB_OK | MB_ICONERROR);
            }
            return true;
        }

        if (id != kIdServicesStartButton && id != kIdServicesStopButton && id != kIdServicesRestartButton)
        {
            return false;
        }

        const int selectedService = SelectedServiceIndex();
        if (selectedService < 0)
        {
            MessageBoxW(hwnd_, L"Select a service first.", L"Services", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        const auto &service = services_[static_cast<size_t>(selectedService)];
        std::wstring error;
        bool actionOk = false;

        if (id == kIdServicesStartButton)
        {
            actionOk = system::services::ServiceManager::StartServiceByName(service.serviceName, error);
        }
        else if (id == kIdServicesStopButton)
        {
            actionOk = system::services::ServiceManager::StopServiceByName(service.serviceName, error);
        }
        else
        {
            actionOk = system::services::ServiceManager::RestartServiceByName(service.serviceName, error);
        }

        if (!actionOk)
        {
            if (error.empty())
            {
                error = L"The service operation failed.";
            }

            std::wstring message = L"Service: " + service.displayName + L"\n\n" + error;
            MessageBoxW(hwnd_, message.c_str(), L"Services", MB_OK | MB_ICONERROR);
            RefreshServicesInventory(true);
            return true;
        }

        RefreshServicesInventory(true);
        return true;
    }

    void MainWindow::RefreshServicesInventory(bool preserveSelection)
    {
        if (!servicesList_)
        {
            return;
        }

        std::wstring selectedServiceName;
        if (preserveSelection)
        {
            const int selected = SelectedServiceIndex();
            if (selected >= 0)
            {
                selectedServiceName = services_[static_cast<size_t>(selected)].serviceName;
            }
        }

        std::vector<system::services::ServiceInfo> updated;
        std::wstring error;
        if (!system::services::ServiceManager::EnumerateServices(updated, error))
        {
            if (servicesStatus_)
            {
                std::wstring status = L"Service scan failed";
                if (!error.empty())
                {
                    status += L": ";
                    status += error;
                }
                SetWindowTextW(servicesStatus_, status.c_str());
            }

            RefreshServicesActionState();
            return;
        }

        services_ = std::move(updated);
        lastServicesRefreshTickMs_ = GetTickCount64();

        ApplyServicesFilterToList(false);

        if (!selectedServiceName.empty())
        {
            for (size_t row = 0; row < servicesVisibleRows_.size(); ++row)
            {
                const size_t index = servicesVisibleRows_[row];
                if (index >= services_.size())
                {
                    continue;
                }

                if (services_[index].serviceName == selectedServiceName)
                {
                    ListView_SetItemState(
                        servicesList_,
                        static_cast<int>(row),
                        LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(servicesList_, static_cast<int>(row), FALSE);
                    break;
                }
            }
        }

        RefreshServicesActionState();
    }

    void MainWindow::ApplyServicesFilterToList(bool preserveSelection)
    {
        if (!servicesList_)
        {
            return;
        }

        std::wstring selectedServiceName;
        if (preserveSelection)
        {
            const int selected = SelectedServiceIndex();
            if (selected >= 0)
            {
                selectedServiceName = services_[static_cast<size_t>(selected)].serviceName;
            }
        }

        const int previousTop = ListView_GetTopIndex(servicesList_);
        const std::wstring loweredQuery = ToLowerServices(servicesFilterText_);

        SendMessageW(servicesList_, WM_SETREDRAW, FALSE, 0);

        ListView_DeleteAllItems(servicesList_);
        servicesVisibleRows_.clear();
        servicesVisibleRows_.reserve(services_.size());

        int selectedRow = -1;

        for (size_t i = 0; i < services_.size(); ++i)
        {
            const auto &service = services_[i];

            bool passMode = false;
            switch (activeServiceFilterMode_)
            {
            case ServiceFilterMode::All:
                passMode = true;
                break;
            case ServiceFilterMode::Running:
                passMode = IsServiceActiveState(service.currentState);
                break;
            case ServiceFilterMode::Stopped:
                passMode = service.currentState == SERVICE_STOPPED;
                break;
            }

            if (!passMode)
            {
                continue;
            }

            const bool passQuery = ContainsInsensitiveServices(service.displayName, loweredQuery) ||
                                   ContainsInsensitiveServices(service.serviceName, loweredQuery) ||
                                   ContainsInsensitiveServices(service.description, loweredQuery) ||
                                   ContainsInsensitiveServices(service.statusText, loweredQuery) ||
                                   ContainsInsensitiveServices(service.startupType, loweredQuery) ||
                                   ContainsInsensitiveServices(service.accountName, loweredQuery);

            if (!passQuery)
            {
                continue;
            }

            const int row = static_cast<int>(servicesVisibleRows_.size());
            servicesVisibleRows_.push_back(i);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(service.displayName.c_str());
            ListView_InsertItem(servicesList_, &item);

            ListView_SetItemText(servicesList_, row, 1, const_cast<LPWSTR>(service.serviceName.c_str()));
            ListView_SetItemText(servicesList_, row, 2, const_cast<LPWSTR>(service.statusText.c_str()));
            ListView_SetItemText(servicesList_, row, 3, const_cast<LPWSTR>(service.startupType.c_str()));

            const std::wstring pidText = service.processId == 0 ? L"-" : std::to_wstring(service.processId);
            ListView_SetItemText(servicesList_, row, 4, const_cast<LPWSTR>(pidText.c_str()));
            ListView_SetItemText(servicesList_, row, 5, const_cast<LPWSTR>(service.accountName.c_str()));
            ListView_SetItemText(servicesList_, row, 6, const_cast<LPWSTR>(service.description.c_str()));

            if (!selectedServiceName.empty() && service.serviceName == selectedServiceName)
            {
                selectedRow = row;
            }
        }

        if (selectedRow >= 0)
        {
            ListView_SetItemState(
                servicesList_,
                selectedRow,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(servicesList_, selectedRow, FALSE);
        }

        const int count = ListView_GetItemCount(servicesList_);
        if (count > 0 && previousTop > 0)
        {
            const int boundedTop = (std::min)(previousTop, count - 1);
            ListView_EnsureVisible(servicesList_, boundedTop, FALSE);
        }

        SendMessageW(servicesList_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(servicesList_, nullptr, TRUE);

        RefreshServicesActionState();
    }

    void MainWindow::RefreshServicesActionState()
    {
        if (!servicesStartButton_ || !servicesStopButton_ || !servicesRestartButton_)
        {
            return;
        }

        const int selected = SelectedServiceIndex();
        if (selected < 0)
        {
            EnableWindow(servicesStartButton_, FALSE);
            EnableWindow(servicesStopButton_, FALSE);
            EnableWindow(servicesRestartButton_, FALSE);

            if (servicesStatus_)
            {
                std::wstringstream summary;
                summary << L"Showing " << servicesVisibleRows_.size() << L" of " << services_.size() << L" services.";
                if (!servicesFilterText_.empty())
                {
                    summary << L" Filter: " << servicesFilterText_;
                }
                SetWindowTextW(servicesStatus_, summary.str().c_str());
            }
            return;
        }

        const auto &service = services_[static_cast<size_t>(selected)];
        const bool running = service.currentState == SERVICE_RUNNING || service.currentState == SERVICE_PAUSED;
        const bool stopped = service.currentState == SERVICE_STOPPED;
        const bool pending = IsServicePendingState(service.currentState);

        EnableWindow(servicesStartButton_, stopped ? TRUE : FALSE);
        EnableWindow(servicesStopButton_, (running && service.canStop && !pending) ? TRUE : FALSE);
        EnableWindow(servicesRestartButton_, (running && service.canStop && !pending) ? TRUE : FALSE);

        if (servicesStatus_)
        {
            std::wstringstream detail;
            detail << L"Selected: " << service.displayName
                   << L" (" << service.serviceName << L")"
                   << L" | " << service.statusText
                   << L" | Startup: " << service.startupType
                   << L" | PID: " << (service.processId == 0 ? L"-" : std::to_wstring(service.processId))
                   << L" | Visible " << servicesVisibleRows_.size() << L"/" << services_.size();

            if (pending)
            {
                detail << L" | State transition in progress";
            }
            else if (running && !service.canStop)
            {
                detail << L" | Stop/Restart not supported";
            }

            SetWindowTextW(servicesStatus_, detail.str().c_str());
        }
    }

} // namespace utm::ui
