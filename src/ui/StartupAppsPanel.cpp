#include "ui/StartupAppsPanel.h"

#include "ui/MainWindow.h"

#include "system/startup/StartupAppsManager.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace
{
    constexpr UINT kIdStartupTitle = 1200;
    constexpr UINT kIdStartupHint = 1201;
    constexpr UINT kIdStartupSearchLabel = 1202;
    constexpr UINT kIdStartupSearchEdit = 1203;
    constexpr UINT kIdStartupUserLabel = 1212;
    constexpr UINT kIdStartupUserCombo = 1213;
    constexpr UINT kIdStartupModeLabel = 1204;
    constexpr UINT kIdStartupModeCombo = 1205;
    constexpr UINT kIdStartupList = 1206;
    constexpr UINT kIdStartupRefreshButton = 1207;
    constexpr UINT kIdStartupEnableButton = 1208;
    constexpr UINT kIdStartupDisableButton = 1209;
    constexpr UINT kIdStartupOpenLocationButton = 1210;

    constexpr const wchar_t *kDefaultStartupTargetId = L"current-user";

    std::wstring ToLowerStartup(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitiveStartup(const std::wstring &text, const std::wstring &loweredQuery)
    {
        if (loweredQuery.empty())
        {
            return true;
        }

        return ToLowerStartup(text).find(loweredQuery) != std::wstring::npos;
    }

    std::wstring ActiveStartupTargetLabel(
        const std::vector<utm::system::startup::StartupUserTarget> &targets,
        const std::wstring &targetId)
    {
        for (const auto &target : targets)
        {
            if (target.id == targetId)
            {
                return target.displayName;
            }
        }

        return L"Current User";
    }

} // namespace

namespace utm::ui
{

    void StartupAppsPanel::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool StartupAppsPanel::HandleCommand(UINT id, UINT code)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleStartupAppsCommand(id, code);
    }

    bool StartupAppsPanel::HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleStartupAppsNotify(hdr, lParam, result);
    }

    int MainWindow::SelectedStartupAppIndex() const
    {
        if (!startupAppsList_)
        {
            return -1;
        }

        const int selectedRow = ListView_GetNextItem(startupAppsList_, -1, LVNI_SELECTED);
        if (selectedRow < 0 || static_cast<size_t>(selectedRow) >= startupAppsVisibleRows_.size())
        {
            return -1;
        }

        const size_t index = startupAppsVisibleRows_[static_cast<size_t>(selectedRow)];
        if (index >= startupApps_.size())
        {
            return -1;
        }

        return static_cast<int>(index);
    }

    void MainWindow::RefreshStartupAppsUserTargets()
    {
        if (!startupAppsUserCombo_)
        {
            return;
        }

        std::wstring targetId = activeStartupAppsTargetId_.empty() ? kDefaultStartupTargetId : activeStartupAppsTargetId_;

        std::vector<system::startup::StartupUserTarget> targets;
        std::wstring discoveryError;
        system::startup::StartupAppsManager::EnumerateStartupUserTargets(targets, discoveryError);

        if (targets.empty())
        {
            targets.push_back({kDefaultStartupTargetId, L"Current User"});
        }

        startupAppsUserTargets_ = std::move(targets);

        SendMessageW(startupAppsUserCombo_, CB_RESETCONTENT, 0, 0);

        int selectedIndex = 0;
        for (size_t i = 0; i < startupAppsUserTargets_.size(); ++i)
        {
            const auto &entry = startupAppsUserTargets_[i];
            SendMessageW(startupAppsUserCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.displayName.c_str()));
            if (entry.id == targetId)
            {
                selectedIndex = static_cast<int>(i);
            }
        }

        SendMessageW(startupAppsUserCombo_, CB_SETCURSEL, selectedIndex, 0);
        if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < startupAppsUserTargets_.size())
        {
            activeStartupAppsTargetId_ = startupAppsUserTargets_[static_cast<size_t>(selectedIndex)].id;
        }
        else
        {
            activeStartupAppsTargetId_ = kDefaultStartupTargetId;
        }
    }

    bool MainWindow::HandleStartupAppsNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!hdr || hdr->hwndFrom != startupAppsList_)
        {
            return false;
        }

        if (hdr->code == LVN_ITEMCHANGED)
        {
            const auto *change = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if (change && (change->uChanged & LVIF_STATE) != 0)
            {
                RefreshStartupAppsActionState();
                result = 0;
                return true;
            }
        }

        return false;
    }

    bool MainWindow::HandleStartupAppsCommand(UINT id, UINT code)
    {
        if (id == kIdStartupSearchEdit && code == EN_CHANGE)
        {
            wchar_t buffer[320]{};
            GetWindowTextW(startupAppsSearchEdit_, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
            startupAppsFilterText_ = buffer;
            ApplyStartupAppsFilterToList(true);
            return true;
        }

        if (id == kIdStartupModeCombo && code == CBN_SELCHANGE)
        {
            const int selected = static_cast<int>(SendMessageW(startupAppsModeCombo_, CB_GETCURSEL, 0, 0));
            switch (selected)
            {
            case 1:
                activeStartupAppsFilterMode_ = StartupAppsFilterMode::Enabled;
                break;
            case 2:
                activeStartupAppsFilterMode_ = StartupAppsFilterMode::Disabled;
                break;
            default:
                activeStartupAppsFilterMode_ = StartupAppsFilterMode::All;
                break;
            }

            ApplyStartupAppsFilterToList(true);
            return true;
        }

        if (id == kIdStartupUserCombo && code == CBN_SELCHANGE)
        {
            const int selected = static_cast<int>(SendMessageW(startupAppsUserCombo_, CB_GETCURSEL, 0, 0));
            if (selected >= 0 && static_cast<size_t>(selected) < startupAppsUserTargets_.size())
            {
                activeStartupAppsTargetId_ = startupAppsUserTargets_[static_cast<size_t>(selected)].id;
            }
            else
            {
                activeStartupAppsTargetId_ = kDefaultStartupTargetId;
            }

            RefreshStartupAppsInventory(false);
            return true;
        }

        if (id == kIdStartupRefreshButton)
        {
            RefreshStartupAppsUserTargets();
            RefreshStartupAppsInventory(true);
            return true;
        }

        if (id == kIdStartupOpenLocationButton)
        {
            const int selectedIndex = SelectedStartupAppIndex();
            if (selectedIndex < 0)
            {
                MessageBoxW(hwnd_, L"Select a startup entry first.", L"Startup Apps", MB_OK | MB_ICONINFORMATION);
                return true;
            }

            const auto &entry = startupApps_[static_cast<size_t>(selectedIndex)];
            if (!entry.canOpenLocation || entry.locationPath.empty())
            {
                MessageBoxW(hwnd_, L"No resolvable file location is available for this startup entry.", L"Startup Apps", MB_OK | MB_ICONINFORMATION);
                return true;
            }

            std::wstring args = L"/select,\"" + entry.locationPath + L"\"";
            const HINSTANCE openResult = ShellExecuteW(hwnd_, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(openResult) <= 32)
            {
                MessageBoxW(hwnd_, L"Unable to open location for the selected startup entry.", L"Startup Apps", MB_OK | MB_ICONERROR);
            }
            return true;
        }

        if (id != kIdStartupEnableButton && id != kIdStartupDisableButton)
        {
            return false;
        }

        const int selectedIndex = SelectedStartupAppIndex();
        if (selectedIndex < 0)
        {
            MessageBoxW(hwnd_, L"Select a startup entry first.", L"Startup Apps", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        const auto &entry = startupApps_[static_cast<size_t>(selectedIndex)];
        if (!entry.canToggle)
        {
            MessageBoxW(hwnd_, L"The selected startup entry cannot be toggled.", L"Startup Apps", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        const bool enable = id == kIdStartupEnableButton;
        std::wstring error;
        if (!system::startup::StartupAppsManager::SetStartupEnabled(entry, enable, error))
        {
            std::wstring message = L"Failed to update startup state for: " + entry.name;
            if (!error.empty())
            {
                message += L"\n\n";
                message += error;
            }

            MessageBoxW(hwnd_, message.c_str(), L"Startup Apps", MB_OK | MB_ICONERROR);
            RefreshStartupAppsInventory(true);
            return true;
        }

        RefreshStartupAppsInventory(true);
        return true;
    }

    void MainWindow::RefreshStartupAppsInventory(bool preserveSelection)
    {
        if (!startupAppsList_)
        {
            return;
        }

        RefreshStartupAppsUserTargets();

        std::wstring selectedId;
        if (preserveSelection)
        {
            const int selectedIndex = SelectedStartupAppIndex();
            if (selectedIndex >= 0)
            {
                selectedId = startupApps_[static_cast<size_t>(selectedIndex)].id;
            }
        }

        std::vector<system::startup::StartupAppEntry> updated;
        std::wstring error;
        if (!system::startup::StartupAppsManager::EnumerateStartupApps(updated, error, activeStartupAppsTargetId_))
        {
            if (startupAppsStatus_)
            {
                std::wstring status = L"Startup inventory failed";
                if (!error.empty())
                {
                    status += L": ";
                    status += error;
                }
                SetWindowTextW(startupAppsStatus_, status.c_str());
            }

            RefreshStartupAppsActionState();
            return;
        }

        startupApps_ = std::move(updated);
        lastStartupAppsRefreshTickMs_ = GetTickCount64();

        ApplyStartupAppsFilterToList(false);

        if (!selectedId.empty())
        {
            for (size_t row = 0; row < startupAppsVisibleRows_.size(); ++row)
            {
                const size_t index = startupAppsVisibleRows_[row];
                if (index >= startupApps_.size())
                {
                    continue;
                }

                if (startupApps_[index].id == selectedId)
                {
                    ListView_SetItemState(
                        startupAppsList_,
                        static_cast<int>(row),
                        LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(startupAppsList_, static_cast<int>(row), FALSE);
                    break;
                }
            }
        }

        RefreshStartupAppsActionState();
    }

    void MainWindow::ApplyStartupAppsFilterToList(bool preserveSelection)
    {
        if (!startupAppsList_)
        {
            return;
        }

        std::wstring selectedId;
        if (preserveSelection)
        {
            const int selected = SelectedStartupAppIndex();
            if (selected >= 0)
            {
                selectedId = startupApps_[static_cast<size_t>(selected)].id;
            }
        }

        const int previousTop = ListView_GetTopIndex(startupAppsList_);
        const std::wstring loweredQuery = ToLowerStartup(startupAppsFilterText_);

        SendMessageW(startupAppsList_, WM_SETREDRAW, FALSE, 0);

        ListView_DeleteAllItems(startupAppsList_);
        startupAppsVisibleRows_.clear();
        startupAppsVisibleRows_.reserve(startupApps_.size());

        int selectedRow = -1;

        for (size_t i = 0; i < startupApps_.size(); ++i)
        {
            const auto &entry = startupApps_[i];

            bool passMode = false;
            switch (activeStartupAppsFilterMode_)
            {
            case StartupAppsFilterMode::All:
                passMode = true;
                break;
            case StartupAppsFilterMode::Enabled:
                passMode = entry.enabled;
                break;
            case StartupAppsFilterMode::Disabled:
                passMode = !entry.enabled;
                break;
            }

            if (!passMode)
            {
                continue;
            }

            const bool passQuery = ContainsInsensitiveStartup(entry.name, loweredQuery) ||
                                   ContainsInsensitiveStartup(entry.command, loweredQuery) ||
                                   ContainsInsensitiveStartup(entry.scopeText, loweredQuery) ||
                                   ContainsInsensitiveStartup(entry.ownerUser, loweredQuery) ||
                                   ContainsInsensitiveStartup(entry.startupType, loweredQuery) ||
                                   ContainsInsensitiveStartup(entry.sourceLocation, loweredQuery) ||
                                   ContainsInsensitiveStartup(entry.statusText, loweredQuery);

            if (!passQuery)
            {
                continue;
            }

            const int row = static_cast<int>(startupAppsVisibleRows_.size());
            startupAppsVisibleRows_.push_back(i);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(entry.name.c_str());
            ListView_InsertItem(startupAppsList_, &item);

            ListView_SetItemText(startupAppsList_, row, 1, const_cast<LPWSTR>(entry.statusText.c_str()));
            ListView_SetItemText(startupAppsList_, row, 2, const_cast<LPWSTR>(entry.startupType.c_str()));
            ListView_SetItemText(startupAppsList_, row, 3, const_cast<LPWSTR>(entry.scopeText.c_str()));
            ListView_SetItemText(startupAppsList_, row, 4, const_cast<LPWSTR>(entry.command.c_str()));
            ListView_SetItemText(startupAppsList_, row, 5, const_cast<LPWSTR>(entry.sourceLocation.c_str()));

            if (!selectedId.empty() && entry.id == selectedId)
            {
                selectedRow = row;
            }
        }

        if (selectedRow >= 0)
        {
            ListView_SetItemState(
                startupAppsList_,
                selectedRow,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(startupAppsList_, selectedRow, FALSE);
        }

        const int count = ListView_GetItemCount(startupAppsList_);
        if (count > 0 && previousTop > 0)
        {
            const int boundedTop = (std::min)(previousTop, count - 1);
            ListView_EnsureVisible(startupAppsList_, boundedTop, FALSE);
        }

        SendMessageW(startupAppsList_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(startupAppsList_, nullptr, TRUE);

        RefreshStartupAppsActionState();
    }

    void MainWindow::RefreshStartupAppsActionState()
    {
        if (!startupAppsEnableButton_ || !startupAppsDisableButton_ || !startupAppsOpenLocationButton_)
        {
            return;
        }

        const int selected = SelectedStartupAppIndex();
        if (selected < 0)
        {
            EnableWindow(startupAppsEnableButton_, FALSE);
            EnableWindow(startupAppsDisableButton_, FALSE);
            EnableWindow(startupAppsOpenLocationButton_, FALSE);

            if (startupAppsStatus_)
            {
                const std::wstring targetLabel = ActiveStartupTargetLabel(startupAppsUserTargets_, activeStartupAppsTargetId_);
                std::wstringstream summary;
                summary << L"Target: " << targetLabel
                        << L" | Showing " << startupAppsVisibleRows_.size() << L" of " << startupApps_.size() << L" startup entries.";
                if (!startupAppsFilterText_.empty())
                {
                    summary << L" Filter: " << startupAppsFilterText_;
                }
                SetWindowTextW(startupAppsStatus_, summary.str().c_str());
            }
            return;
        }

        const auto &entry = startupApps_[static_cast<size_t>(selected)];
        EnableWindow(startupAppsEnableButton_, (!entry.enabled && entry.canToggle) ? TRUE : FALSE);
        EnableWindow(startupAppsDisableButton_, (entry.enabled && entry.canToggle) ? TRUE : FALSE);
        EnableWindow(startupAppsOpenLocationButton_, entry.canOpenLocation ? TRUE : FALSE);

        if (startupAppsStatus_)
        {
             const std::wstring targetLabel = ActiveStartupTargetLabel(startupAppsUserTargets_, activeStartupAppsTargetId_);
            std::wstringstream detail;
            detail << L"Selected: " << entry.name
                   << L" | " << entry.statusText
                   << L" | " << entry.startupType
                   << L" | " << entry.scopeText
                 << L" | Target: " << targetLabel
                   << L" | Visible " << startupAppsVisibleRows_.size() << L"/" << startupApps_.size();

            if (!entry.canToggle)
            {
                detail << L" | Toggle unavailable";
            }
            if (!entry.canOpenLocation)
            {
                detail << L" | Location unresolved";
            }

            SetWindowTextW(startupAppsStatus_, detail.str().c_str());
        }
    }

} // namespace utm::ui
