#include "ui/ProcessListView.h"

#include "ui/MainWindow.h"

#include "tools/process/ProcessActions.h"

#include <commctrl.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace
{
    constexpr UINT kIdFilterEdit = 1022;

    constexpr UINT kCommandEndTask = 40001;
    constexpr UINT kCommandEndTree = 40002;
    constexpr UINT kCommandSuspend = 40003;
    constexpr UINT kCommandResume = 40004;
    constexpr UINT kCommandOpenLocation = 40005;
    constexpr UINT kCommandProperties = 40006;

    constexpr UINT kCommandPriorityIdle = 40100;
    constexpr UINT kCommandPriorityBelowNormal = 40101;
    constexpr UINT kCommandPriorityNormal = 40102;
    constexpr UINT kCommandPriorityAboveNormal = 40103;
    constexpr UINT kCommandPriorityHigh = 40104;
    constexpr UINT kCommandPriorityRealtime = 40105;

    constexpr UINT kCommandAffinityAll = 40200;
    constexpr UINT kCommandAffinityCore0 = 40201;
    constexpr UINT kCommandAffinityCore1 = 40202;

    std::wstring ToLowerLocal(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitiveLocal(const std::wstring &text, const std::wstring &query)
    {
        if (query.empty())
        {
            return true;
        }

        const std::wstring loweredText = ToLowerLocal(text);
        const std::wstring loweredQuery = ToLowerLocal(query);
        return loweredText.find(loweredQuery) != std::wstring::npos;
    }
}

namespace utm::ui
{

    void ProcessListView::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool ProcessListView::HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!owner_ || !hdr || hdr->hwndFrom != owner_->processList_)
        {
            return false;
        }

        if (hdr->code == LVN_GETDISPINFOW)
        {
            auto *info = reinterpret_cast<NMLVDISPINFOW *>(lParam);
            if (!info)
            {
                result = 0;
                return true;
            }

            if ((info->item.mask & LVIF_TEXT) != 0)
            {
                const std::wstring text = owner_->CellText(static_cast<size_t>(info->item.iItem), info->item.iSubItem);
                StringCchCopyW(info->item.pszText, info->item.cchTextMax, text.c_str());
            }

            result = 0;
            return true;
        }

        if (hdr->code == LVN_COLUMNCLICK)
        {
            const auto *click = reinterpret_cast<const NMLISTVIEW *>(lParam);
            const auto clicked = static_cast<MainWindow::SortColumn>(std::clamp(click->iSubItem, 0, 10));

            if (clicked == owner_->sortColumn_)
            {
                owner_->sortAscending_ = !owner_->sortAscending_;
            }
            else
            {
                owner_->sortColumn_ = clicked;
                owner_->sortAscending_ = clicked == MainWindow::SortColumn::Name || clicked == MainWindow::SortColumn::Pid;
            }

            owner_->ApplyFilterAndSort();
            owner_->RefreshProcessView();
            result = 0;
            return true;
        }

        if (hdr->code == NM_RCLICK)
        {
            DWORD pos = GetMessagePos();
            POINT pt{GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
            owner_->ShowProcessContextMenu(pt);
            result = 0;
            return true;
        }

        return false;
    }

    bool ProcessListView::HandleCommand(UINT id, UINT code)
    {
        if (!owner_)
        {
            return false;
        }

        if (id == kIdFilterEdit && code == EN_CHANGE)
        {
            wchar_t buffer[256]{};
            GetWindowTextW(owner_->filterEdit_, buffer, static_cast<int>(std::size(buffer)));
            owner_->filterText_ = buffer;
            owner_->ApplyFilterAndSort();
            owner_->RefreshProcessView();
            return true;
        }

        return false;
    }

    void MainWindow::HandleSnapshotUpdate()
    {
        snapshot_ = engine_.GetSnapshot();
        ApplyFilterAndSort();
        UpdatePerformanceMetrics();
        RefreshPerformancePanel();

        if (activeSection_ == Section::Network)
        {
            const std::uint64_t nowMs = GetTickCount64();
            if (nowMs >= lastNetworkRefreshTickMs_ + 1200)
            {
                RefreshNetworkInventory(true);
            }
        }

        if (activeSection_ == Section::Hardware)
        {
            const std::uint64_t nowMs = GetTickCount64();
            if (nowMs >= lastHardwareRefreshTickMs_ + 12000)
            {
                RefreshHardwareInventory(true);
            }
        }

        if (activeSection_ == Section::Services)
        {
            const std::uint64_t nowMs = GetTickCount64();
            if (nowMs >= lastServicesRefreshTickMs_ + 5000)
            {
                RefreshServicesInventory(true);
            }
        }

        if (activeSection_ == Section::StartupApps)
        {
            const std::uint64_t nowMs = GetTickCount64();
            if (nowMs >= lastStartupAppsRefreshTickMs_ + 8000)
            {
                RefreshStartupAppsInventory(true);
            }
        }

        if (activeSection_ == Section::Users)
        {
            const std::uint64_t nowMs = GetTickCount64();
            if (nowMs >= lastUsersRefreshTickMs_ + 2500)
            {
                RefreshUsersInventory(true);
            }
        }

        if (activeSection_ == Section::Processes)
        {
            RefreshProcessView();
        }

        std::wstringstream status;
        status << L"Processes: " << snapshot_.processes.size()
               << L" | Elevated: " << (snapshot_.runtime.isElevated ? L"Yes" : L"No")
               << L" | SeDebug: " << (snapshot_.runtime.seDebugEnabled ? L"Enabled" : L"Unavailable");

        ShowStatusText(status.str());
    }

    void MainWindow::RefreshProcessView()
    {
        if (!processList_)
        {
            return;
        }

        const int itemCount = static_cast<int>(visibleRows_.size());
        if (itemCount != lastVisibleCount_)
        {
            ListView_SetItemCountEx(processList_, itemCount, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
            lastVisibleCount_ = itemCount;
        }

        if (activeSection_ != Section::Processes || !IsWindowVisible(processList_))
        {
            return;
        }

        RedrawWindow(processList_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
    }

    void MainWindow::ApplyFilterAndSort()
    {
        visibleRows_.clear();
        visibleRows_.reserve(snapshot_.processes.size());

        for (size_t i = 0; i < snapshot_.processes.size(); ++i)
        {
            const auto &p = snapshot_.processes[i];

            bool pass = true;
            if (!filterText_.empty())
            {
                pass = ContainsInsensitiveLocal(p.imageName, filterText_);
                if (!pass)
                {
                    const std::wstring pidText = std::to_wstring(p.pid);
                    pass = ContainsInsensitiveLocal(pidText, filterText_);
                }
            }

            if (pass)
            {
                visibleRows_.push_back(i);
            }
        }

        auto cmp = [&](size_t a, size_t b)
        {
            const auto &left = snapshot_.processes[a];
            const auto &right = snapshot_.processes[b];

            auto compare = [&](auto lv, auto rv)
            {
                if (lv < rv)
                {
                    return -1;
                }
                if (lv > rv)
                {
                    return 1;
                }
                return 0;
            };

            int result = 0;
            switch (sortColumn_)
            {
            case SortColumn::Name:
                result = compare(ToLowerLocal(left.imageName), ToLowerLocal(right.imageName));
                break;
            case SortColumn::Pid:
                result = compare(left.pid, right.pid);
                break;
            case SortColumn::Cpu:
                result = compare(left.cpuPercent, right.cpuPercent);
                break;
            case SortColumn::WorkingSet:
                result = compare(left.workingSetBytes, right.workingSetBytes);
                break;
            case SortColumn::PrivateBytes:
                result = compare(left.privateBytes, right.privateBytes);
                break;
            case SortColumn::Threads:
                result = compare(left.threadCount, right.threadCount);
                break;
            case SortColumn::Handles:
                result = compare(left.handleCount, right.handleCount);
                break;
            case SortColumn::ParentPid:
                result = compare(left.parentPid, right.parentPid);
                break;
            case SortColumn::Priority:
                result = compare(left.priorityClass, right.priorityClass);
                break;
            case SortColumn::ReadBytes:
                result = compare(left.readBytes, right.readBytes);
                break;
            case SortColumn::WriteBytes:
                result = compare(left.writeBytes, right.writeBytes);
                break;
            }

            if (result == 0)
            {
                result = compare(left.pid, right.pid);
            }

            return sortAscending_ ? (result < 0) : (result > 0);
        };

        std::sort(visibleRows_.begin(), visibleRows_.end(), cmp);
    }

    std::uint32_t MainWindow::GetSelectedPid() const
    {
        const int selected = ListView_GetNextItem(processList_, -1, LVNI_SELECTED);
        if (selected < 0 || static_cast<size_t>(selected) >= visibleRows_.size())
        {
            return 0;
        }

        const size_t snapshotIndex = visibleRows_[selected];
        if (snapshotIndex >= snapshot_.processes.size())
        {
            return 0;
        }

        return snapshot_.processes[snapshotIndex].pid;
    }

    void MainWindow::ShowProcessContextMenu(POINT screenPoint)
    {
        contextPid_ = GetSelectedPid();
        if (contextPid_ == 0)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        HMENU priorityMenu = CreatePopupMenu();
        HMENU affinityMenu = CreatePopupMenu();

        AppendMenuW(menu, MF_STRING, kCommandEndTask, L"End Task");
        AppendMenuW(menu, MF_STRING, kCommandEndTree, L"End Process Tree");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandSuspend, L"Suspend");
        AppendMenuW(menu, MF_STRING, kCommandResume, L"Resume");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(priorityMenu, MF_STRING, kCommandPriorityIdle, L"Idle");
        AppendMenuW(priorityMenu, MF_STRING, kCommandPriorityBelowNormal, L"Below Normal");
        AppendMenuW(priorityMenu, MF_STRING, kCommandPriorityNormal, L"Normal");
        AppendMenuW(priorityMenu, MF_STRING, kCommandPriorityAboveNormal, L"Above Normal");
        AppendMenuW(priorityMenu, MF_STRING, kCommandPriorityHigh, L"High");
        AppendMenuW(priorityMenu, MF_STRING, kCommandPriorityRealtime, L"Realtime");

        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(priorityMenu), L"Set Priority");

        DWORD_PTR processMask = 0;
        DWORD_PTR systemMask = 0;
        GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);

        AppendMenuW(affinityMenu, MF_STRING, kCommandAffinityAll, L"All Cores");
        AppendMenuW(affinityMenu, MF_STRING, kCommandAffinityCore0, L"Core 0");

        if ((systemMask & 0x2) != 0)
        {
            AppendMenuW(affinityMenu, MF_STRING, kCommandAffinityCore1, L"Core 1");
        }

        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(affinityMenu), L"Set Affinity");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandOpenLocation, L"Open File Location");
        AppendMenuW(menu, MF_STRING, kCommandProperties, L"Properties");

        TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
        DestroyMenu(menu);
    }

    void MainWindow::ExecuteProcessCommand(UINT commandId)
    {
        if (contextPid_ == 0)
        {
            contextPid_ = GetSelectedPid();
        }

        if (contextPid_ == 0)
        {
            return;
        }

        std::wstring error;
        bool ok = false;

        switch (commandId)
        {
        case kCommandEndTask:
            ok = tools::process::ProcessActions::SmartTerminate(contextPid_, 1200, error);
            break;
        case kCommandEndTree:
            ok = tools::process::ProcessActions::TerminateTree(contextPid_, error);
            break;
        case kCommandSuspend:
            ok = tools::process::ProcessActions::Suspend(contextPid_, error);
            break;
        case kCommandResume:
            ok = tools::process::ProcessActions::Resume(contextPid_, error);
            break;
        case kCommandOpenLocation:
            ok = tools::process::ProcessActions::OpenFileLocation(contextPid_, error);
            break;
        case kCommandProperties:
            ok = tools::process::ProcessActions::OpenProperties(contextPid_, error);
            break;
        case kCommandPriorityIdle:
            ok = tools::process::ProcessActions::SetPriority(contextPid_, IDLE_PRIORITY_CLASS, error);
            break;
        case kCommandPriorityBelowNormal:
            ok = tools::process::ProcessActions::SetPriority(contextPid_, BELOW_NORMAL_PRIORITY_CLASS, error);
            break;
        case kCommandPriorityNormal:
            ok = tools::process::ProcessActions::SetPriority(contextPid_, NORMAL_PRIORITY_CLASS, error);
            break;
        case kCommandPriorityAboveNormal:
            ok = tools::process::ProcessActions::SetPriority(contextPid_, ABOVE_NORMAL_PRIORITY_CLASS, error);
            break;
        case kCommandPriorityHigh:
            ok = tools::process::ProcessActions::SetPriority(contextPid_, HIGH_PRIORITY_CLASS, error);
            break;
        case kCommandPriorityRealtime:
            if (MessageBoxW(hwnd_, L"Realtime priority can destabilize the system. Continue?", L"Confirm", MB_ICONWARNING | MB_YESNO) == IDYES)
            {
                ok = tools::process::ProcessActions::SetPriority(contextPid_, REALTIME_PRIORITY_CLASS, error);
            }
            break;
        case kCommandAffinityAll:
        {
            DWORD_PTR processMask = 0;
            DWORD_PTR systemMask = 0;
            GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
            ok = tools::process::ProcessActions::SetAffinity(contextPid_, systemMask, error);
            break;
        }
        case kCommandAffinityCore0:
            ok = tools::process::ProcessActions::SetAffinity(contextPid_, 0x1, error);
            break;
        case kCommandAffinityCore1:
            ok = tools::process::ProcessActions::SetAffinity(contextPid_, 0x2, error);
            break;
        default:
            break;
        }

        if (!ok && !error.empty())
        {
            MessageBoxW(hwnd_, error.c_str(), L"Operation Failed", MB_OK | MB_ICONERROR);
        }
    }

    std::wstring MainWindow::CellText(size_t row, int subItem) const
    {
        if (row >= visibleRows_.size())
        {
            return L"";
        }

        const size_t idx = visibleRows_[row];
        if (idx >= snapshot_.processes.size())
        {
            return L"";
        }

        const auto &p = snapshot_.processes[idx];

        switch (subItem)
        {
        case 0:
            return p.imageName;
        case 1:
            return std::to_wstring(p.pid);
        case 2:
            return FormatCpu(p.cpuPercent);
        case 3:
            return FormatBytes(p.workingSetBytes);
        case 4:
            return FormatBytes(p.privateBytes);
        case 5:
            return std::to_wstring(p.threadCount);
        case 6:
            return std::to_wstring(p.handleCount);
        case 7:
            return std::to_wstring(p.parentPid);
        case 8:
            return PriorityClassToText(p.priorityClass);
        case 9:
            return FormatBytes(p.readBytes);
        case 10:
            return FormatBytes(p.writeBytes);
        default:
            return L"";
        }
    }

    std::wstring MainWindow::FormatBytes(std::uint64_t bytes)
    {
        constexpr double kb = 1024.0;
        constexpr double mb = kb * 1024.0;
        constexpr double gb = mb * 1024.0;

        wchar_t buffer[64]{};

        if (bytes >= static_cast<std::uint64_t>(gb))
        {
            StringCchPrintfW(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.2f GB", bytes / gb);
            return buffer;
        }

        if (bytes >= static_cast<std::uint64_t>(mb))
        {
            StringCchPrintfW(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.2f MB", bytes / mb);
            return buffer;
        }

        if (bytes >= static_cast<std::uint64_t>(kb))
        {
            StringCchPrintfW(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.2f KB", bytes / kb);
            return buffer;
        }

        StringCchPrintfW(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%llu B", bytes);
        return buffer;
    }

    std::wstring MainWindow::FormatCpu(double cpu)
    {
        wchar_t buffer[32]{};
        StringCchPrintfW(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.1f", cpu);
        return buffer;
    }

    std::wstring MainWindow::PriorityClassToText(std::uint32_t priorityClass)
    {
        switch (priorityClass)
        {
        case IDLE_PRIORITY_CLASS:
            return L"Idle";
        case BELOW_NORMAL_PRIORITY_CLASS:
            return L"Below Normal";
        case NORMAL_PRIORITY_CLASS:
            return L"Normal";
        case ABOVE_NORMAL_PRIORITY_CLASS:
            return L"Above Normal";
        case HIGH_PRIORITY_CLASS:
            return L"High";
        case REALTIME_PRIORITY_CLASS:
            return L"Realtime";
        default:
            return L"Unknown";
        }
    }

} // namespace utm::ui
