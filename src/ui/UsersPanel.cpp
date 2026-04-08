#include "ui/UsersPanel.h"

#include "ui/MainWindow.h"

#include "system/users/UserSessionManager.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace
{
    constexpr UINT kIdUsersTitle = 1220;
    constexpr UINT kIdUsersHint = 1221;
    constexpr UINT kIdUsersSearchLabel = 1222;
    constexpr UINT kIdUsersSearchEdit = 1223;
    constexpr UINT kIdUsersModeLabel = 1224;
    constexpr UINT kIdUsersModeCombo = 1225;
    constexpr UINT kIdUsersList = 1226;
    constexpr UINT kIdUsersRefreshButton = 1227;
    constexpr UINT kIdUsersLogoffButton = 1228;
    constexpr UINT kIdUsersDisconnectButton = 1229;
    constexpr UINT kIdUsersStatus = 1230;
    constexpr UINT kIdUsersProcessUserCombo = 1233;
    constexpr UINT kIdUsersProcessList = 1234;

    std::wstring ToLowerUsers(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitiveUsers(const std::wstring &text, const std::wstring &loweredQuery)
    {
        if (loweredQuery.empty())
        {
            return true;
        }

        return ToLowerUsers(text).find(loweredQuery) != std::wstring::npos;
    }

    std::wstring FormatRateMBps(double value)
    {
        std::wstringstream out;
        out << std::fixed << std::setprecision(value < 10.0 ? 2 : 1) << value << L" MB/s";
        return out.str();
    }

    std::wstring FormatRateMbps(double value)
    {
        std::wstringstream out;
        out << std::fixed << std::setprecision(value < 10.0 ? 2 : 1) << value << L" Mbps";
        return out.str();
    }

} // namespace

namespace utm::ui
{

    void UsersPanel::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool UsersPanel::HandleCommand(UINT id, UINT code)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleUsersCommand(id, code);
    }

    bool UsersPanel::HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleUsersNotify(hdr, lParam, result);
    }

    int MainWindow::SelectedUserIndex() const
    {
        if (!usersList_)
        {
            return -1;
        }

        const int selectedRow = ListView_GetNextItem(usersList_, -1, LVNI_SELECTED);
        if (selectedRow < 0 || static_cast<size_t>(selectedRow) >= usersVisibleRows_.size())
        {
            return -1;
        }

        const size_t index = usersVisibleRows_[static_cast<size_t>(selectedRow)];
        if (index >= usersSessions_.size())
        {
            return -1;
        }

        return static_cast<int>(index);
    }

    bool MainWindow::HandleUsersNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!hdr)
        {
            return false;
        }

        if (hdr->hwndFrom == usersList_ && hdr->code == LVN_ITEMCHANGED)
        {
            const auto *change = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if (change && (change->uChanged & LVIF_STATE) != 0)
            {
                RefreshUsersActionState();
                result = 0;
                return true;
            }
        }

        if (hdr->hwndFrom == usersProcessList_ && hdr->code == LVN_ITEMCHANGED)
        {
            const auto *change = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if (change && (change->uChanged & LVIF_STATE) != 0)
            {
                RefreshUsersActionState();
                result = 0;
                return true;
            }
        }

        return false;
    }

    bool MainWindow::HandleUsersCommand(UINT id, UINT code)
    {
        if (id == kIdUsersSearchEdit && code == EN_CHANGE)
        {
            wchar_t buffer[320]{};
            GetWindowTextW(usersSearchEdit_, buffer, static_cast<int>(std::size(buffer)));
            usersFilterText_ = buffer;
            ApplyUsersFilterToList(true);
            return true;
        }

        if (id == kIdUsersModeCombo && code == CBN_SELCHANGE)
        {
            const int selected = static_cast<int>(SendMessageW(usersModeCombo_, CB_GETCURSEL, 0, 0));
            switch (selected)
            {
            case 1:
                activeUsersFilterMode_ = UsersFilterMode::Active;
                break;
            case 2:
                activeUsersFilterMode_ = UsersFilterMode::Disconnected;
                break;
            default:
                activeUsersFilterMode_ = UsersFilterMode::All;
                break;
            }

            ApplyUsersFilterToList(true);
            return true;
        }

        if (id == kIdUsersProcessUserCombo && code == CBN_SELCHANGE)
        {
            const int selected = static_cast<int>(SendMessageW(usersProcessUserCombo_, CB_GETCURSEL, 0, 0));
            if (selected >= 0 && static_cast<size_t>(selected) < usersProcessTargets_.size())
            {
                activeUsersProcessTarget_ = usersProcessTargets_[static_cast<size_t>(selected)].first;
            }
            else
            {
                activeUsersProcessTarget_ = L"*";
            }

            ApplyUsersProcessFilterToList(true);
            RefreshUsersActionState();
            return true;
        }

        if (id == kIdUsersRefreshButton)
        {
            RefreshUsersInventory(true);
            return true;
        }

        if (id != kIdUsersLogoffButton && id != kIdUsersDisconnectButton)
        {
            return false;
        }

        const int selected = SelectedUserIndex();
        if (selected < 0)
        {
            MessageBoxW(hwnd_, L"Select a user session first.", L"Users", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        const auto &session = usersSessions_[static_cast<size_t>(selected)];
        if (id == kIdUsersLogoffButton)
        {
            if (!session.canLogoff)
            {
                MessageBoxW(hwnd_, L"The selected session cannot be logged off.", L"Users", MB_OK | MB_ICONINFORMATION);
                return true;
            }

            std::wstring confirmation = L"Log off " + session.accountName + L"? Unsaved work in that session may be lost.";
            if (MessageBoxW(hwnd_, confirmation.c_str(), L"Users", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            {
                return true;
            }

            std::wstring error;
            if (!system::users::UserSessionManager::LogoffSession(session.sessionId, error))
            {
                std::wstring message = L"Unable to log off selected session.";
                if (!error.empty())
                {
                    message += L"\n\n" + error;
                }
                MessageBoxW(hwnd_, message.c_str(), L"Users", MB_OK | MB_ICONERROR);
                return true;
            }

            RefreshUsersInventory(true);
            return true;
        }

        if (!session.canDisconnect)
        {
            MessageBoxW(hwnd_, L"The selected session cannot be disconnected.", L"Users", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        std::wstring confirmation = L"Disconnect " + session.accountName + L"? The session stays signed in but will be disconnected.";
        if (MessageBoxW(hwnd_, confirmation.c_str(), L"Users", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            return true;
        }

        std::wstring error;
        if (!system::users::UserSessionManager::DisconnectSession(session.sessionId, error))
        {
            std::wstring message = L"Unable to disconnect selected session.";
            if (!error.empty())
            {
                message += L"\n\n" + error;
            }
            MessageBoxW(hwnd_, message.c_str(), L"Users", MB_OK | MB_ICONERROR);
            return true;
        }

        RefreshUsersInventory(true);
        return true;
    }

    void MainWindow::RefreshUsersInventory(bool preserveSelection)
    {
        if (!usersList_)
        {
            return;
        }

        DWORD selectedSessionId = static_cast<DWORD>(-1);
        if (preserveSelection)
        {
            const int selected = SelectedUserIndex();
            if (selected >= 0)
            {
                selectedSessionId = usersSessions_[static_cast<size_t>(selected)].sessionId;
            }
        }

        std::vector<system::users::UserSessionInfo> updated;
        std::wstring error;
        if (!system::users::UserSessionManager::EnumerateUserSessions(snapshot_, updated, error))
        {
            if (usersStatus_)
            {
                std::wstring status = L"Unable to load user sessions";
                if (!error.empty())
                {
                    status += L": " + error;
                }
                SetWindowTextW(usersStatus_, status.c_str());
            }

            RefreshUsersActionState();
            return;
        }

        std::vector<system::users::UserProcessInfo> updatedProcesses;
        std::wstring processError;
        if (!system::users::UserSessionManager::EnumerateUserProcesses(snapshot_, updatedProcesses, processError))
        {
            updatedProcesses.clear();
        }

        const std::uint64_t nowMs = GetTickCount64();
        const double deltaSeconds =
            usersIoSampleTickMs_ > 0 && nowMs > usersIoSampleTickMs_
                ? static_cast<double>(nowMs - usersIoSampleTickMs_) / 1000.0
                : 0.0;
        const double processDeltaSeconds =
            usersProcessIoSampleTickMs_ > 0 && nowMs > usersProcessIoSampleTickMs_
                ? static_cast<double>(nowMs - usersProcessIoSampleTickMs_) / 1000.0
                : 0.0;

        std::unordered_map<DWORD, std::pair<std::uint64_t, std::uint64_t>> newIoTotals;
        newIoTotals.reserve(updated.size());

        std::unordered_map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> newProcessIoTotals;
        newProcessIoTotals.reserve(updatedProcesses.size());

        for (auto &session : updated)
        {
            newIoTotals[session.sessionId] = {session.totalReadBytes, session.totalWriteBytes};
            session.diskMBps = 0.0;
            session.networkMbps = 0.0;

            if (deltaSeconds <= 0.05)
            {
                continue;
            }

            const auto previous = usersPreviousIoTotals_.find(session.sessionId);
            if (previous == usersPreviousIoTotals_.end())
            {
                continue;
            }

            const std::uint64_t previousRead = previous->second.first;
            const std::uint64_t previousWrite = previous->second.second;

            const std::uint64_t deltaRead = session.totalReadBytes >= previousRead
                                                ? session.totalReadBytes - previousRead
                                                : 0;
            const std::uint64_t deltaWrite = session.totalWriteBytes >= previousWrite
                                                 ? session.totalWriteBytes - previousWrite
                                                 : 0;

            session.diskMBps = static_cast<double>(deltaRead + deltaWrite) / (1024.0 * 1024.0 * deltaSeconds);
        }

        for (auto &process : updatedProcesses)
        {
            newProcessIoTotals[process.pid] = {process.readBytes, process.writeBytes};
            process.diskMBps = 0.0;

            if (processDeltaSeconds <= 0.05)
            {
                continue;
            }

            const auto previous = usersProcessPreviousIoTotals_.find(process.pid);
            if (previous == usersProcessPreviousIoTotals_.end())
            {
                continue;
            }

            const std::uint64_t previousRead = previous->second.first;
            const std::uint64_t previousWrite = previous->second.second;
            const std::uint64_t deltaRead = process.readBytes >= previousRead ? process.readBytes - previousRead : 0;
            const std::uint64_t deltaWrite = process.writeBytes >= previousWrite ? process.writeBytes - previousWrite : 0;

            process.diskMBps = static_cast<double>(deltaRead + deltaWrite) / (1024.0 * 1024.0 * processDeltaSeconds);
        }

        usersPreviousIoTotals_ = std::move(newIoTotals);
        usersIoSampleTickMs_ = nowMs;
        usersProcessPreviousIoTotals_ = std::move(newProcessIoTotals);
        usersProcessIoSampleTickMs_ = nowMs;

        usersSessions_ = std::move(updated);
        usersProcesses_ = std::move(updatedProcesses);
        lastUsersRefreshTickMs_ = nowMs;

        RefreshUsersProcessTargets();
        ApplyUsersFilterToList(false);

        if (selectedSessionId != static_cast<DWORD>(-1))
        {
            for (size_t row = 0; row < usersVisibleRows_.size(); ++row)
            {
                const size_t index = usersVisibleRows_[row];
                if (index >= usersSessions_.size())
                {
                    continue;
                }

                if (usersSessions_[index].sessionId == selectedSessionId)
                {
                    ListView_SetItemState(
                        usersList_,
                        static_cast<int>(row),
                        LVIS_SELECTED | LVIS_FOCUSED,
                        LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(usersList_, static_cast<int>(row), FALSE);
                    break;
                }
            }
        }

        RefreshUsersActionState();
    }

    void MainWindow::ApplyUsersFilterToList(bool preserveSelection)
    {
        if (!usersList_)
        {
            return;
        }

        DWORD selectedSessionId = static_cast<DWORD>(-1);
        if (preserveSelection)
        {
            const int selected = SelectedUserIndex();
            if (selected >= 0)
            {
                selectedSessionId = usersSessions_[static_cast<size_t>(selected)].sessionId;
            }
        }

        const int previousTop = ListView_GetTopIndex(usersList_);
        const std::wstring loweredQuery = ToLowerUsers(usersFilterText_);

        SendMessageW(usersList_, WM_SETREDRAW, FALSE, 0);

        ListView_DeleteAllItems(usersList_);
        usersVisibleRows_.clear();
        usersVisibleRows_.reserve(usersSessions_.size());

        int selectedRow = -1;

        for (size_t i = 0; i < usersSessions_.size(); ++i)
        {
            const auto &session = usersSessions_[i];

            bool passMode = false;
            switch (activeUsersFilterMode_)
            {
            case UsersFilterMode::All:
                passMode = true;
                break;
            case UsersFilterMode::Active:
                passMode = session.connectState == WTSActive;
                break;
            case UsersFilterMode::Disconnected:
                passMode = session.connectState == WTSDisconnected;
                break;
            }

            if (!passMode)
            {
                continue;
            }

            const std::wstring sessionIdText = std::to_wstring(session.sessionId);
            const bool passQuery = ContainsInsensitiveUsers(session.accountName, loweredQuery) ||
                                   ContainsInsensitiveUsers(session.stateText, loweredQuery) ||
                                   ContainsInsensitiveUsers(session.sessionName, loweredQuery) ||
                                   ContainsInsensitiveUsers(sessionIdText, loweredQuery);

            if (!passQuery)
            {
                continue;
            }

            const int row = static_cast<int>(usersVisibleRows_.size());
            usersVisibleRows_.push_back(i);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(session.accountName.c_str());
            ListView_InsertItem(usersList_, &item);

            std::wstring cpuText = FormatCpu(session.cpuPercent);
            std::wstring memoryText = FormatBytes(session.memoryBytes);
            std::wstring diskText = FormatRateMBps(session.diskMBps);
            std::wstring networkText = FormatRateMbps(session.networkMbps);
            std::wstring processText = std::to_wstring(session.processCount);
            std::wstring sessionName = session.sessionName.empty() ? L"-" : session.sessionName;

            ListView_SetItemText(usersList_, row, 1, const_cast<LPWSTR>(session.stateText.c_str()));
            ListView_SetItemText(usersList_, row, 2, cpuText.data());
            ListView_SetItemText(usersList_, row, 3, memoryText.data());
            ListView_SetItemText(usersList_, row, 4, diskText.data());
            ListView_SetItemText(usersList_, row, 5, networkText.data());
            ListView_SetItemText(usersList_, row, 6, processText.data());
            ListView_SetItemText(usersList_, row, 7, const_cast<LPWSTR>(sessionIdText.c_str()));
            ListView_SetItemText(usersList_, row, 8, sessionName.data());

            if (session.sessionId == selectedSessionId)
            {
                selectedRow = row;
            }
        }

        if (selectedRow >= 0)
        {
            ListView_SetItemState(
                usersList_,
                selectedRow,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(usersList_, selectedRow, FALSE);
        }

        const int itemCount = ListView_GetItemCount(usersList_);
        if (itemCount > 0 && previousTop > 0)
        {
            const int boundedTop = (std::min)(previousTop, itemCount - 1);
            ListView_EnsureVisible(usersList_, boundedTop, FALSE);
        }

        SendMessageW(usersList_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(usersList_, nullptr, TRUE);

        ApplyUsersProcessFilterToList(preserveSelection);
        RefreshUsersActionState();
    }

    void MainWindow::RefreshUsersActionState()
    {
        if (!usersLogoffButton_ || !usersDisconnectButton_)
        {
            return;
        }

        std::wstring targetLabel = L"All Users";
        for (const auto &entry : usersProcessTargets_)
        {
            if (entry.first == activeUsersProcessTarget_)
            {
                targetLabel = entry.second;
                break;
            }
        }

        const int selectedProcess = SelectedUsersProcessIndex();

        const int selected = SelectedUserIndex();
        if (selected < 0)
        {
            EnableWindow(usersLogoffButton_, FALSE);
            EnableWindow(usersDisconnectButton_, FALSE);

            if (usersStatus_)
            {
                std::wstringstream summary;
                summary << L"Showing " << usersVisibleRows_.size() << L" of " << usersSessions_.size() << L" sessions.";
                summary << L" Process Scope: " << targetLabel << L" (" << usersProcessVisibleRows_.size() << L" rows).";
                if (!usersFilterText_.empty())
                {
                    summary << L" Filter: " << usersFilterText_;
                }
                SetWindowTextW(usersStatus_, summary.str().c_str());
            }
            return;
        }

        const auto &session = usersSessions_[static_cast<size_t>(selected)];
        EnableWindow(usersLogoffButton_, session.canLogoff ? TRUE : FALSE);
        EnableWindow(usersDisconnectButton_, session.canDisconnect ? TRUE : FALSE);

        if (usersStatus_)
        {
            std::wstringstream detail;
            detail << L"Selected: " << session.accountName
                   << L" | State: " << session.stateText
                   << L" | CPU: " << FormatCpu(session.cpuPercent)
                   << L" | Memory: " << FormatBytes(session.memoryBytes)
                   << L" | Disk: " << FormatRateMBps(session.diskMBps)
                   << L" | Processes: " << session.processCount
                   << L" | Process Scope: " << targetLabel
                   << L" (" << usersProcessVisibleRows_.size() << L" rows)";

            if (selectedProcess >= 0)
            {
                const auto &process = usersProcesses_[static_cast<size_t>(selectedProcess)];
                detail << L" | Process: " << process.imageName
                       << L" (PID " << process.pid << L")"
                       << L" CPU " << FormatCpu(process.cpuPercent)
                       << L" Mem " << FormatBytes(process.memoryBytes);
            }

            SetWindowTextW(usersStatus_, detail.str().c_str());
        }
    }

} // namespace utm::ui
