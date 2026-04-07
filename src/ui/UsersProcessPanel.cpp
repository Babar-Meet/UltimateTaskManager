#include "ui/MainWindow.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace
{
    std::wstring ToLowerUsersProcess(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitiveUsersProcess(const std::wstring &text, const std::wstring &loweredQuery)
    {
        if (loweredQuery.empty())
        {
            return true;
        }

        return ToLowerUsersProcess(text).find(loweredQuery) != std::wstring::npos;
    }

    std::wstring FormatDiskRate(double value)
    {
        std::wstringstream out;
        out << std::fixed << std::setprecision(value < 10.0 ? 2 : 1) << value << L" MB/s";
        return out.str();
    }

} // namespace

namespace utm::ui
{

    int MainWindow::SelectedUsersProcessIndex() const
    {
        if (!usersProcessList_)
        {
            return -1;
        }

        const int selectedRow = ListView_GetNextItem(usersProcessList_, -1, LVNI_SELECTED);
        if (selectedRow < 0 || static_cast<size_t>(selectedRow) >= usersProcessVisibleRows_.size())
        {
            return -1;
        }

        const size_t index = usersProcessVisibleRows_[static_cast<size_t>(selectedRow)];
        if (index >= usersProcesses_.size())
        {
            return -1;
        }

        return static_cast<int>(index);
    }

    void MainWindow::RefreshUsersProcessTargets()
    {
        if (!usersProcessUserCombo_)
        {
            return;
        }

        const std::wstring previousTarget = activeUsersProcessTarget_.empty() ? L"*" : activeUsersProcessTarget_;

        usersProcessTargets_.clear();
        usersProcessTargets_.push_back({L"*", L"All Users"});

        std::vector<std::wstring> accounts;
        accounts.reserve(usersSessions_.size() + usersProcesses_.size());

        auto appendAccount = [&](const std::wstring &name)
        {
            if (!name.empty())
            {
                accounts.push_back(name);
            }
        };

        for (const auto &session : usersSessions_)
        {
            appendAccount(session.accountName);
        }

        for (const auto &process : usersProcesses_)
        {
            appendAccount(process.accountName);
        }

        std::sort(accounts.begin(), accounts.end());

        std::unordered_set<std::wstring> seen;
        seen.reserve(accounts.size() * 2 + 1);

        for (const auto &name : accounts)
        {
            const std::wstring lowered = ToLowerUsersProcess(name);
            if (!seen.insert(lowered).second)
            {
                continue;
            }

            usersProcessTargets_.push_back({name, name});
        }

        SendMessageW(usersProcessUserCombo_, CB_RESETCONTENT, 0, 0);

        int selectedIndex = 0;
        for (size_t i = 0; i < usersProcessTargets_.size(); ++i)
        {
            const auto &entry = usersProcessTargets_[i];
            SendMessageW(usersProcessUserCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.second.c_str()));
            if (entry.first == previousTarget)
            {
                selectedIndex = static_cast<int>(i);
            }
        }

        SendMessageW(usersProcessUserCombo_, CB_SETCURSEL, selectedIndex, 0);
        if (selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < usersProcessTargets_.size())
        {
            activeUsersProcessTarget_ = usersProcessTargets_[static_cast<size_t>(selectedIndex)].first;
        }
        else
        {
            activeUsersProcessTarget_ = L"*";
        }
    }

    void MainWindow::ApplyUsersProcessFilterToList(bool preserveSelection)
    {
        if (!usersProcessList_)
        {
            return;
        }

        std::uint32_t selectedPid = 0;
        DWORD selectedSessionId = 0;
        if (preserveSelection)
        {
            const int selected = SelectedUsersProcessIndex();
            if (selected >= 0)
            {
                const auto &entry = usersProcesses_[static_cast<size_t>(selected)];
                selectedPid = entry.pid;
                selectedSessionId = entry.sessionId;
            }
        }

        const std::wstring loweredQuery = ToLowerUsersProcess(usersFilterText_);
        const std::wstring loweredTarget = ToLowerUsersProcess(activeUsersProcessTarget_);
        const int previousTop = ListView_GetTopIndex(usersProcessList_);

        SendMessageW(usersProcessList_, WM_SETREDRAW, FALSE, 0);

        ListView_DeleteAllItems(usersProcessList_);
        usersProcessVisibleRows_.clear();
        usersProcessVisibleRows_.reserve(usersProcesses_.size());

        int selectedRow = -1;

        for (size_t i = 0; i < usersProcesses_.size(); ++i)
        {
            const auto &process = usersProcesses_[i];

            const bool passTarget = loweredTarget == L"*" || ToLowerUsersProcess(process.accountName) == loweredTarget;
            if (!passTarget)
            {
                continue;
            }

            const std::wstring pidText = std::to_wstring(process.pid);
            const std::wstring sessionText = std::to_wstring(process.sessionId);
            const bool passQuery = ContainsInsensitiveUsersProcess(process.accountName, loweredQuery) ||
                                   ContainsInsensitiveUsersProcess(process.imageName, loweredQuery) ||
                                   ContainsInsensitiveUsersProcess(pidText, loweredQuery) ||
                                   ContainsInsensitiveUsersProcess(sessionText, loweredQuery);

            if (!passQuery)
            {
                continue;
            }

            const int row = static_cast<int>(usersProcessVisibleRows_.size());
            usersProcessVisibleRows_.push_back(i);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(process.accountName.c_str());
            ListView_InsertItem(usersProcessList_, &item);

            std::wstring cpuText = FormatCpu(process.cpuPercent);
            std::wstring memoryText = FormatBytes(process.memoryBytes);
            std::wstring diskText = FormatDiskRate(process.diskMBps);
            std::wstring readText = FormatBytes(process.readBytes);
            std::wstring writeText = FormatBytes(process.writeBytes);

            ListView_SetItemText(usersProcessList_, row, 1, const_cast<LPWSTR>(process.imageName.c_str()));
            ListView_SetItemText(usersProcessList_, row, 2, const_cast<LPWSTR>(pidText.c_str()));
            ListView_SetItemText(usersProcessList_, row, 3, cpuText.data());
            ListView_SetItemText(usersProcessList_, row, 4, memoryText.data());
            ListView_SetItemText(usersProcessList_, row, 5, diskText.data());
            ListView_SetItemText(usersProcessList_, row, 6, readText.data());
            ListView_SetItemText(usersProcessList_, row, 7, writeText.data());
            ListView_SetItemText(usersProcessList_, row, 8, const_cast<LPWSTR>(sessionText.c_str()));

            if (preserveSelection && process.pid == selectedPid && process.sessionId == selectedSessionId)
            {
                selectedRow = row;
            }
        }

        if (selectedRow >= 0)
        {
            ListView_SetItemState(
                usersProcessList_,
                selectedRow,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(usersProcessList_, selectedRow, FALSE);
        }

        const int itemCount = ListView_GetItemCount(usersProcessList_);
        if (itemCount > 0 && previousTop > 0)
        {
            const int boundedTop = (std::min)(previousTop, itemCount - 1);
            ListView_EnsureVisible(usersProcessList_, boundedTop, FALSE);
        }

        SendMessageW(usersProcessList_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(usersProcessList_, nullptr, TRUE);
    }

} // namespace utm::ui
