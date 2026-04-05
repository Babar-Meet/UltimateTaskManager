#include <winsock2.h>

#include "ui/MainWindow.h"

#include "system/ntapi/NtApi.h"
#include "tools/process/ProcessActions.h"
#include "util/logging/Logger.h"

#include <commctrl.h>
#include <commdlg.h>
#include <iphlpapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "UxTheme.lib")

namespace
{

    constexpr int kSidebarWidth = 252;
    constexpr int kPadding = 12;
    constexpr int kStatusHeight = 28;
    constexpr int kFilterHeight = 28;
    constexpr int kNavButtonHeight = 34;

    constexpr COLORREF kMainBackgroundColor = RGB(243, 246, 252);
    constexpr COLORREF kSidebarBackgroundColor = RGB(234, 239, 248);
    constexpr COLORREF kCardColor = RGB(255, 255, 255);

    constexpr int kIdSidebar = 1000;
    constexpr int kIdSidebarTitle = 1001;
    constexpr int kIdNavProcesses = 1010;
    constexpr int kIdNavPerformance = 1011;
    constexpr int kIdNavNetwork = 1012;
    constexpr int kIdNavHardware = 1013;
    constexpr int kIdNavServices = 1014;
    constexpr int kIdNavStartupApps = 1015;
    constexpr int kIdNavUsers = 1016;
    constexpr int kIdNavQuickTools = 1017;

    constexpr int kIdSectionTitle = 1020;
    constexpr int kIdFilterLabel = 1021;
    constexpr int kIdFilterEdit = 1022;
    constexpr int kIdProcessList = 1023;
    constexpr int kIdStatus = 1024;
    constexpr int kIdPerfPlaceholder = 1025;
    constexpr int kIdNetworkPlaceholder = 1026;
    constexpr int kIdHardwarePlaceholder = 1027;
    constexpr int kIdServicesPlaceholder = 1028;
    constexpr int kIdStartupAppsPlaceholder = 1029;
    constexpr int kIdUsersPlaceholder = 1030;
    constexpr int kIdQuickToolsTitle = 1031;
    constexpr int kIdQuickToolsHint = 1032;
    constexpr int kIdQuickPortLabel = 1033;
    constexpr int kIdQuickPortEdit = 1034;
    constexpr int kIdQuickProcessLabel = 1035;
    constexpr int kIdQuickProcessEdit = 1036;
    constexpr int kIdQuickDeleteLabel = 1037;
    constexpr int kIdQuickDeletePathEdit = 1038;

    constexpr int kIdQuickToolPortKillAll = 41000;
    constexpr int kIdQuickToolProcessKillAll = 41001;
    constexpr int kIdQuickToolSmartDelete = 41002;
    constexpr int kIdQuickToolPortKillOneByOne = 41003;
    constexpr int kIdQuickToolProcessKillOneByOne = 41004;
    constexpr int kIdQuickToolBrowseFile = 41005;
    constexpr int kIdQuickToolBrowseFolder = 41006;

    constexpr int kIdPerfCoreGrid = 43000;
    constexpr int kIdPerfGraphCpu = 43001;
    constexpr int kIdPerfGraphMemory = 43002;
    constexpr int kIdPerfGraphGpu = 43003;
    constexpr int kIdPerfGraphUpload = 43004;
    constexpr int kIdPerfGraphDownload = 43005;

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

    constexpr double kBytesPerGiB = 1024.0 * 1024.0 * 1024.0;

    COLORREF BlendColor(COLORREF a, COLORREF b, double mix)
    {
        mix = (std::clamp)(mix, 0.0, 1.0);

        const auto blend = [&](BYTE from, BYTE to)
        {
            const double value = static_cast<double>(from) +
                                 (static_cast<double>(to) - static_cast<double>(from)) * mix;
            return static_cast<BYTE>((std::clamp)(value, 0.0, 255.0));
        };

        return RGB(
            blend(GetRValue(a), GetRValue(b)),
            blend(GetGValue(a), GetGValue(b)),
            blend(GetBValue(a), GetBValue(b)));
    }

    std::wstring FormatNumber(double value, int decimals)
    {
        std::wostringstream out;
        out << std::fixed << std::setprecision(decimals) << value;
        return out.str();
    }

    std::wstring FormatAxisValue(double value, const wchar_t *unit)
    {
        int decimals = 0;
        if (value < 10.0)
        {
            decimals = 1;
        }

        std::wstring text = FormatNumber(value, decimals);
        if (unit && unit[0] != L'\0')
        {
            text += L" ";
            text += unit;
        }

        return text;
    }

    HMENU MenuId(int id)
    {
        return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
    }

    std::wstring ToLower(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitive(const std::wstring &text, const std::wstring &query)
    {
        if (query.empty())
        {
            return true;
        }

        const std::wstring loweredText = ToLower(text);
        const std::wstring loweredQuery = ToLower(query);
        return loweredText.find(loweredQuery) != std::wstring::npos;
    }

    std::wstring Win32ErrorToText(DWORD error)
    {
        if (error == 0)
        {
            return L"0";
        }

        wchar_t *buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = FormatMessageW(
            flags,
            nullptr,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        std::wstringstream out;
        out << error;

        if (length > 0 && buffer)
        {
            std::wstring message(buffer, length);
            while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
            {
                message.pop_back();
            }

            if (!message.empty())
            {
                out << L" (" << message << L")";
            }
        }

        if (buffer)
        {
            LocalFree(buffer);
        }

        return out.str();
    }

    std::uint16_t LocalPortFromRow(DWORD netOrderPort)
    {
        const std::uint16_t p = static_cast<std::uint16_t>(netOrderPort & 0xFFFF);
        return static_cast<std::uint16_t>((p >> 8) | (p << 8));
    }

    std::set<DWORD> PidsUsingPort(std::uint16_t port)
    {
        std::set<DWORD> pids;

        ULONG size = 0;
        if (GetExtendedTcpTable(nullptr, &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<std::uint8_t> buffer(size);
            if (GetExtendedTcpTable(buffer.data(), &size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR)
            {
                const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(buffer.data());
                for (DWORD i = 0; i < table->dwNumEntries; ++i)
                {
                    const auto &row = table->table[i];
                    if (LocalPortFromRow(row.dwLocalPort) == port)
                    {
                        pids.insert(row.dwOwningPid);
                    }
                }
            }
        }

        size = 0;
        if (GetExtendedUdpTable(nullptr, &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0) == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<std::uint8_t> buffer(size);
            if (GetExtendedUdpTable(buffer.data(), &size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR)
            {
                const auto *table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID *>(buffer.data());
                for (DWORD i = 0; i < table->dwNumEntries; ++i)
                {
                    const auto &row = table->table[i];
                    if (LocalPortFromRow(row.dwLocalPort) == port)
                    {
                        pids.insert(row.dwOwningPid);
                    }
                }
            }
        }

        pids.erase(0);
        pids.erase(4);
        pids.erase(GetCurrentProcessId());
        return pids;
    }

    bool KillPidList(const std::set<DWORD> &pids, int timeoutMs, int &successCount, int &failureCount, std::wstring &lastError)
    {
        successCount = 0;
        failureCount = 0;
        lastError.clear();

        for (const DWORD pid : pids)
        {
            std::wstring error;
            if (utm::tools::process::ProcessActions::SmartTerminate(pid, timeoutMs, error))
            {
                ++successCount;
            }
            else
            {
                ++failureCount;
                if (!error.empty())
                {
                    lastError = error;
                }
            }
        }

        return failureCount == 0;
    }

    std::set<DWORD> PidsMatchingPattern(const utm::core::model::SystemSnapshot &snapshot, const std::wstring &pattern)
    {
        std::set<DWORD> pids;
        const std::wstring loweredPattern = ToLower(pattern);
        if (loweredPattern.empty())
        {
            return pids;
        }

        for (const auto &item : snapshot.processes)
        {
            const std::wstring loweredName = ToLower(item.imageName);
            if (loweredName.find(loweredPattern) != std::wstring::npos)
            {
                pids.insert(item.pid);
            }
        }

        pids.erase(0);
        pids.erase(4);
        pids.erase(GetCurrentProcessId());
        return pids;
    }

    std::vector<std::wstring> SplitTokens(const std::wstring &text)
    {
        std::vector<std::wstring> out;
        std::wstring current;

        const auto flush = [&]()
        {
            if (!current.empty())
            {
                out.push_back(current);
                current.clear();
            }
        };

        for (const wchar_t ch : text)
        {
            if (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n' || ch == L',' || ch == L';')
            {
                flush();
            }
            else
            {
                current.push_back(ch);
            }
        }

        flush();
        return out;
    }

    std::vector<std::uint16_t> ParsePorts(const std::wstring &input)
    {
        std::vector<std::uint16_t> ports;
        std::set<std::uint16_t> dedup;

        for (const auto &token : SplitTokens(input))
        {
            bool numeric = !token.empty();
            for (const wchar_t ch : token)
            {
                if (ch < L'0' || ch > L'9')
                {
                    numeric = false;
                    break;
                }
            }

            if (!numeric)
            {
                continue;
            }

            const unsigned long value = std::wcstoul(token.c_str(), nullptr, 10);
            if (value == 0 || value > 65535)
            {
                continue;
            }

            const auto p = static_cast<std::uint16_t>(value);
            if (dedup.insert(p).second)
            {
                ports.push_back(p);
            }
        }

        return ports;
    }

    std::wstring PidLabel(const utm::core::model::SystemSnapshot &snapshot, DWORD pid)
    {
        for (const auto &p : snapshot.processes)
        {
            if (p.pid == pid)
            {
                std::wstring label = p.imageName;
                label += L" (PID ";
                label += std::to_wstring(pid);
                label += L")";
                return label;
            }
        }

        std::wstring fallback = L"PID ";
        fallback += std::to_wstring(pid);
        return fallback;
    }

    bool BrowseForFile(HWND owner, std::wstring &outPath)
    {
        wchar_t filePath[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"All Files\0*.*\0";
        ofn.lpstrFile = filePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        ofn.lpstrTitle = L"Select File To Force Delete";

        if (!GetOpenFileNameW(&ofn))
        {
            return false;
        }

        outPath = filePath;
        return true;
    }

    bool BrowseForFolder(HWND owner, std::wstring &outPath)
    {
        BROWSEINFOW bi{};
        bi.hwndOwner = owner;
        bi.lpszTitle = L"Select Folder To Force Delete";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (!pidl)
        {
            return false;
        }

        wchar_t selected[MAX_PATH]{};
        const bool ok = SHGetPathFromIDListW(pidl, selected) == TRUE;
        CoTaskMemFree(pidl);

        if (!ok)
        {
            return false;
        }

        outPath = selected;
        return true;
    }

} // namespace

namespace utm::ui
{

    MainWindow::MainWindow(HINSTANCE instance, core::model::RuntimeState runtimeState)
        : instance_(instance),
          engine_(std::chrono::milliseconds(800)),
          runtimeState_(runtimeState)
    {
        backgroundBrush_ = CreateSolidBrush(kMainBackgroundColor);
        sidebarBrush_ = CreateSolidBrush(kSidebarBackgroundColor);
        cardBrush_ = CreateSolidBrush(kCardColor);

        uiFont_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        uiBoldFont_ = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        uiTitleFont_ = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        if (PdhOpenQueryW(nullptr, 0, &gpuQuery_) == ERROR_SUCCESS)
        {
            if (PdhAddEnglishCounterW(gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_) == ERROR_SUCCESS)
            {
                gpuCounterReady_ = PdhCollectQueryData(gpuQuery_) == ERROR_SUCCESS;
            }
            else
            {
                PdhCloseQuery(gpuQuery_);
                gpuQuery_ = nullptr;
                gpuCounter_ = nullptr;
            }
        }
    }

    MainWindow::~MainWindow()
    {
        engine_.Stop();

        if (backgroundBrush_)
        {
            DeleteObject(backgroundBrush_);
        }

        if (sidebarBrush_)
        {
            DeleteObject(sidebarBrush_);
        }

        if (cardBrush_)
        {
            DeleteObject(cardBrush_);
        }

        if (uiFont_)
        {
            DeleteObject(uiFont_);
        }

        if (uiBoldFont_)
        {
            DeleteObject(uiBoldFont_);
        }

        if (uiTitleFont_)
        {
            DeleteObject(uiTitleFont_);
        }

        if (gpuQuery_)
        {
            PdhCloseQuery(gpuQuery_);
            gpuQuery_ = nullptr;
            gpuCounter_ = nullptr;
        }
    }

    bool MainWindow::CreateAndShow(int showCommand)
    {
        if (!RegisterWindowClass())
        {
            util::logging::Logger::Instance().Write(
                util::logging::LogLevel::Error,
                L"RegisterWindowClass failed. LastError=" + Win32ErrorToText(GetLastError()));
            return false;
        }

        hwnd_ = CreateWindowExW(
            0,
            L"UTM_MainWindow",
            L"Ultimate Task Manager",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1460,
            900,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_)
        {
            util::logging::Logger::Instance().Write(
                util::logging::LogLevel::Error,
                L"CreateWindowExW main window failed. LastError=" + Win32ErrorToText(GetLastError()));
            return false;
        }

        const int safeShow = showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand;
        ShowWindow(hwnd_, safeShow);
        UpdateWindow(hwnd_);

        return true;
    }

    int MainWindow::RunMessageLoop()
    {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        return static_cast<int>(msg.wParam);
    }

    bool MainWindow::RegisterWindowClass()
    {
        SetLastError(0);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &MainWindow::WndProcSetup;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = backgroundBrush_;
        wc.lpszClassName = L"UTM_MainWindow";

        if (RegisterClassExW(&wc) != 0)
        {
            return true;
        }

        const DWORD error = GetLastError();
        return error == ERROR_CLASS_ALREADY_EXISTS;
    }

    LRESULT CALLBACK MainWindow::WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            auto *self = reinterpret_cast<MainWindow *>(create->lpCreateParams);

            self->hwnd_ = hwnd;

            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MainWindow::WndProcThunk));
            return self->WndProc(hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK MainWindow::WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto *self = reinterpret_cast<MainWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        return self->WndProc(hwnd, message, wParam, lParam);
    }

    LRESULT MainWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            if (!CreateChildControls())
            {
                return -1;
            }

            engine_.SetRuntimeState(runtimeState_);
            engine_.SetUiNotification(hwnd_, kSnapshotMessage);
            engine_.Start();
            return 0;
        }
        case WM_SIZE:
            LayoutControls();
            return 0;

        case kSnapshotMessage:
            HandleSnapshotUpdate();
            return 0;

        case WM_NOTIFY:
        {
            const auto *hdr = reinterpret_cast<const NMHDR *>(lParam);
            if (!hdr)
            {
                break;
            }

            if (hdr->hwndFrom == processList_)
            {
                if (hdr->code == LVN_GETDISPINFOW)
                {
                    auto *info = reinterpret_cast<NMLVDISPINFOW *>(lParam);
                    if (!info)
                    {
                        return 0;
                    }

                    if ((info->item.mask & LVIF_TEXT) != 0)
                    {
                        const std::wstring text = CellText(static_cast<size_t>(info->item.iItem), info->item.iSubItem);
                        StringCchCopyW(info->item.pszText, info->item.cchTextMax, text.c_str());
                    }
                    return 0;
                }

                if (hdr->code == LVN_COLUMNCLICK)
                {
                    const auto *click = reinterpret_cast<const NMLISTVIEW *>(lParam);
                    const auto clicked = static_cast<SortColumn>(std::clamp(click->iSubItem, 0, 10));

                    if (clicked == sortColumn_)
                    {
                        sortAscending_ = !sortAscending_;
                    }
                    else
                    {
                        sortColumn_ = clicked;
                        sortAscending_ = clicked == SortColumn::Name || clicked == SortColumn::Pid;
                    }

                    ApplyFilterAndSort();
                    RefreshProcessView();
                    return 0;
                }

                if (hdr->code == NM_RCLICK)
                {
                    DWORD pos = GetMessagePos();
                    POINT pt{GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
                    ShowProcessContextMenu(pt);
                    return 0;
                }
            }

            break;
        }

        case WM_COMMAND:
        {
            const UINT id = LOWORD(wParam);
            const UINT code = HIWORD(wParam);

            if (id == kIdNavProcesses)
            {
                SetActiveSection(Section::Processes);
                return 0;
            }

            if (id == kIdNavPerformance)
            {
                SetActiveSection(Section::Performance);
                return 0;
            }

            if (id == kIdNavNetwork)
            {
                SetActiveSection(Section::Network);
                return 0;
            }

            if (id == kIdNavHardware)
            {
                SetActiveSection(Section::Hardware);
                return 0;
            }

            if (id == kIdNavServices)
            {
                SetActiveSection(Section::Services);
                return 0;
            }

            if (id == kIdNavStartupApps)
            {
                SetActiveSection(Section::StartupApps);
                return 0;
            }

            if (id == kIdNavUsers)
            {
                SetActiveSection(Section::Users);
                return 0;
            }

            if (id == kIdNavQuickTools)
            {
                SetActiveSection(Section::QuickKillTools);
                return 0;
            }

            if (id == kIdFilterEdit && code == EN_CHANGE)
            {
                wchar_t buffer[256]{};
                GetWindowTextW(filterEdit_, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
                filterText_ = buffer;
                ApplyFilterAndSort();
                RefreshProcessView();
                return 0;
            }

            if (id >= kCommandEndTask && id <= kCommandAffinityCore1)
            {
                ExecuteProcessCommand(id);
                return 0;
            }

            if (id == kIdQuickToolPortKillAll || id == kIdQuickToolPortKillOneByOne)
            {
                wchar_t input[512]{};
                GetWindowTextW(quickPortEdit_, input, static_cast<int>(std::size(input)));
                const auto ports = ParsePorts(input);
                if (ports.empty())
                {
                    MessageBoxW(hwnd_, L"Enter one or more ports (examples: 5000 3000 3690 6699).", L"Port Killer", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }

                std::unordered_map<DWORD, std::wstring> reasonsByPid;
                std::set<DWORD> orderedPids;

                for (const auto port : ports)
                {
                    const auto pids = PidsUsingPort(port);
                    for (const auto pid : pids)
                    {
                        orderedPids.insert(pid);
                        auto &reason = reasonsByPid[pid];
                        if (!reason.empty())
                        {
                            reason += L", ";
                        }
                        reason += L"port ";
                        reason += std::to_wstring(port);
                    }
                }

                if (orderedPids.empty())
                {
                    MessageBoxW(hwnd_, L"No process currently uses the entered port(s).", L"Port Killer", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }

                int killed = 0;
                int failed = 0;
                int skipped = 0;
                std::wstring lastError;

                if (id == kIdQuickToolPortKillOneByOne)
                {
                    for (const auto pid : orderedPids)
                    {
                        std::wstring prompt = L"Kill this process?\n\n";
                        prompt += PidLabel(snapshot_, pid);
                        prompt += L"\nUsing: ";
                        prompt += reasonsByPid[pid];
                        prompt += L"\n\nYes = kill, No = skip, Cancel = stop.";

                        const int decision = MessageBoxW(hwnd_, prompt.c_str(), L"Port Killer (One By One)", MB_ICONQUESTION | MB_YESNOCANCEL);
                        if (decision == IDCANCEL)
                        {
                            break;
                        }
                        if (decision == IDNO)
                        {
                            ++skipped;
                            continue;
                        }

                        std::wstring error;
                        if (tools::process::ProcessActions::SmartTerminate(pid, 1500, error))
                        {
                            ++killed;
                        }
                        else
                        {
                            ++failed;
                            if (!error.empty())
                            {
                                lastError = error;
                            }
                        }
                    }
                }
                else
                {
                    int success = 0;
                    int failures = 0;
                    KillPidList(orderedPids, 1500, success, failures, lastError);
                    killed = success;
                    failed = failures;
                }

                std::wstring result = L"Port killer summary\n\nKilled: ";
                result += std::to_wstring(killed);
                result += L"\nSkipped: ";
                result += std::to_wstring(skipped);
                result += L"\nFailed: ";
                result += std::to_wstring(failed);
                if (!lastError.empty())
                {
                    result += L"\n\nLast error: ";
                    result += lastError;
                }

                MessageBoxW(hwnd_, result.c_str(), L"Port Killer", failed > 0 ? MB_OK | MB_ICONWARNING : MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            if (id == kIdQuickToolProcessKillAll || id == kIdQuickToolProcessKillOneByOne)
            {
                wchar_t input[512]{};
                GetWindowTextW(quickProcessEdit_, input, static_cast<int>(std::size(input)));
                const auto tokens = SplitTokens(input);
                if (tokens.empty())
                {
                    MessageBoxW(hwnd_, L"Enter one or more process patterns (examples: node ollama chrome).", L"Process Killer", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }

                std::unordered_map<DWORD, std::wstring> reasonsByPid;
                std::set<DWORD> orderedPids;

                for (const auto &token : tokens)
                {
                    const auto matches = PidsMatchingPattern(snapshot_, token);
                    for (const auto pid : matches)
                    {
                        orderedPids.insert(pid);
                        auto &reason = reasonsByPid[pid];
                        if (!reason.empty())
                        {
                            reason += L", ";
                        }
                        reason += token;
                    }
                }

                if (orderedPids.empty())
                {
                    MessageBoxW(hwnd_, L"No running process matches the entered pattern(s).", L"Process Killer", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }

                int killed = 0;
                int failed = 0;
                int skipped = 0;
                std::wstring lastError;

                if (id == kIdQuickToolProcessKillOneByOne)
                {
                    for (const auto pid : orderedPids)
                    {
                        std::wstring prompt = L"Kill this process?\n\n";
                        prompt += PidLabel(snapshot_, pid);
                        prompt += L"\nMatched: ";
                        prompt += reasonsByPid[pid];
                        prompt += L"\n\nYes = kill, No = skip, Cancel = stop.";

                        const int decision = MessageBoxW(hwnd_, prompt.c_str(), L"Process Killer (One By One)", MB_ICONQUESTION | MB_YESNOCANCEL);
                        if (decision == IDCANCEL)
                        {
                            break;
                        }
                        if (decision == IDNO)
                        {
                            ++skipped;
                            continue;
                        }

                        std::wstring error;
                        if (tools::process::ProcessActions::SmartTerminate(pid, 1500, error))
                        {
                            ++killed;
                        }
                        else
                        {
                            ++failed;
                            if (!error.empty())
                            {
                                lastError = error;
                            }
                        }
                    }
                }
                else
                {
                    int success = 0;
                    int failures = 0;
                    KillPidList(orderedPids, 1500, success, failures, lastError);
                    killed = success;
                    failed = failures;
                }

                std::wstring result = L"Process killer summary\n\nKilled: ";
                result += std::to_wstring(killed);
                result += L"\nSkipped: ";
                result += std::to_wstring(skipped);
                result += L"\nFailed: ";
                result += std::to_wstring(failed);
                if (!lastError.empty())
                {
                    result += L"\n\nLast error: ";
                    result += lastError;
                }

                MessageBoxW(hwnd_, result.c_str(), L"Process Killer", failed > 0 ? MB_OK | MB_ICONWARNING : MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            if (id == kIdQuickToolBrowseFile)
            {
                std::wstring path;
                if (BrowseForFile(hwnd_, path))
                {
                    quickDeleteTargetPath_ = path;
                    quickDeleteTargetIsDirectory_ = false;
                    SetWindowTextW(quickDeletePathEdit_, quickDeleteTargetPath_.c_str());
                }
                return 0;
            }

            if (id == kIdQuickToolBrowseFolder)
            {
                std::wstring path;
                if (BrowseForFolder(hwnd_, path))
                {
                    quickDeleteTargetPath_ = path;
                    quickDeleteTargetIsDirectory_ = true;
                    SetWindowTextW(quickDeletePathEdit_, quickDeleteTargetPath_.c_str());
                }
                return 0;
            }

            if (id == kIdQuickToolSmartDelete)
            {
                if (quickDeleteTargetPath_.empty())
                {
                    MessageBoxW(hwnd_, L"Select a file or folder first.", L"Force Delete", MB_OK | MB_ICONINFORMATION);
                    return 0;
                }

                std::wstring error;
                bool isDirectory = quickDeleteTargetIsDirectory_;
                if (!isDirectory)
                {
                    std::error_code ec;
                    isDirectory = std::filesystem::is_directory(std::filesystem::path(quickDeleteTargetPath_), ec);
                }
                if (tools::process::ProcessActions::ForceDeletePath(quickDeleteTargetPath_, isDirectory, error))
                {
                    std::wstring msg = L"Deleted successfully:\n";
                    msg += quickDeleteTargetPath_;
                    MessageBoxW(hwnd_, msg.c_str(), L"Force Delete", MB_OK | MB_ICONINFORMATION);

                    quickDeleteTargetPath_.clear();
                    quickDeleteTargetIsDirectory_ = false;
                    SetWindowTextW(quickDeletePathEdit_, L"");
                }
                else
                {
                    if (error.empty())
                    {
                        error = L"Force delete failed.";
                    }
                    MessageBoxW(hwnd_, error.c_str(), L"Force Delete", MB_OK | MB_ICONERROR);
                }
                return 0;
            }

            break;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, TRANSPARENT);

            if (control == sidebar_ || GetParent(control) == sidebar_)
            {
                SetBkColor(dc, kSidebarBackgroundColor);
                SetTextColor(dc, RGB(38, 50, 68));
                return reinterpret_cast<LRESULT>(sidebarBrush_);
            }

            if (control == sectionTitle_ || control == quickToolsTitle_)
            {
                SetBkColor(dc, kMainBackgroundColor);
                SetTextColor(dc, RGB(28, 41, 63));
                return reinterpret_cast<LRESULT>(backgroundBrush_);
            }

            if (control == performancePlaceholder_)
            {
                SetBkColor(dc, kMainBackgroundColor);
                SetTextColor(dc, RGB(40, 57, 82));
                return reinterpret_cast<LRESULT>(backgroundBrush_);
            }

            if (control == quickToolsHint_ ||
                control == performancePlaceholder_ ||
                control == networkPlaceholder_ ||
                control == hardwarePlaceholder_ ||
                control == servicesPlaceholder_ ||
                control == startupAppsPlaceholder_ ||
                control == usersPlaceholder_)
            {
                SetBkColor(dc, kMainBackgroundColor);
                SetTextColor(dc, RGB(76, 92, 118));
                return reinterpret_cast<LRESULT>(backgroundBrush_);
            }

            if (control == statusText_)
            {
                SetBkColor(dc, kMainBackgroundColor);
                SetTextColor(dc, RGB(86, 100, 122));
                return reinterpret_cast<LRESULT>(backgroundBrush_);
            }

            if (control == quickDeletePathEdit_)
            {
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, kCardColor);
                SetTextColor(dc, RGB(28, 41, 63));
                return reinterpret_cast<LRESULT>(cardBrush_);
            }

            SetBkColor(dc, kMainBackgroundColor);
            SetTextColor(dc, RGB(44, 56, 76));
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }

        case WM_CTLCOLOREDIT:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kCardColor);
            SetTextColor(dc, RGB(28, 41, 63));
            return reinterpret_cast<LRESULT>(cardBrush_);
        }

        case WM_CTLCOLORBTN:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            if (control == navProcesses_ ||
                control == navPerformance_ ||
                control == navNetwork_ ||
                control == navHardware_ ||
                control == navServices_ ||
                control == navStartupApps_ ||
                control == navUsers_ ||
                control == navQuickTools_)
            {
                SetBkColor(dc, kSidebarBackgroundColor);
                SetTextColor(dc, RGB(32, 48, 76));
                return reinterpret_cast<LRESULT>(sidebarBrush_);
            }

            SetBkColor(dc, kMainBackgroundColor);
            SetTextColor(dc, RGB(28, 41, 63));
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }

        case WM_ERASEBKGND:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            FillRect(dc, &rc, backgroundBrush_);
            return 1;
        }

        case WM_DRAWITEM:
        {
            const auto *draw = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
            if (!draw)
            {
                break;
            }

            if (draw->CtlID == kIdPerfGraphCpu ||
                draw->CtlID == kIdPerfGraphMemory ||
                draw->CtlID == kIdPerfGraphGpu ||
                draw->CtlID == kIdPerfGraphUpload ||
                draw->CtlID == kIdPerfGraphDownload)
            {
                DrawPerformanceGraph(draw);
                return TRUE;
            }

            if (draw->CtlID == kIdPerfCoreGrid)
            {
                DrawCoreGrid(draw);
                return TRUE;
            }

            break;
        }

        case WM_DESTROY:
            engine_.Stop();
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool MainWindow::CreateChildControls()
    {
        auto applyFont = [](HWND control, HFONT font)
        {
            if (control && font)
            {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
        };

        sidebar_ = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdSidebar),
            instance_,
            nullptr);

        if (!sidebar_)
        {
            util::logging::Logger::Instance().Write(
                util::logging::LogLevel::Error,
                L"CreateWindowExW sidebar failed. LastError=" + Win32ErrorToText(GetLastError()));
            return false;
        }

        sidebarTitle_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Task Manager",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            sidebar_,
            MenuId(kIdSidebarTitle),
            instance_,
            nullptr);

        auto createNavButton = [&](int id, const wchar_t *text, DWORD extraStyle)
        {
            return CreateWindowExW(
                0,
                L"BUTTON",
                text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | extraStyle,
                0,
                0,
                0,
                0,
                hwnd_,
                MenuId(id),
                instance_,
                nullptr);
        };

        navProcesses_ = createNavButton(kIdNavProcesses, L"Processes", WS_GROUP);
        navPerformance_ = createNavButton(kIdNavPerformance, L"Performance", 0);
        navNetwork_ = createNavButton(kIdNavNetwork, L"Network", 0);
        navHardware_ = createNavButton(kIdNavHardware, L"Hardware", 0);
        navServices_ = createNavButton(kIdNavServices, L"Services", 0);
        navStartupApps_ = createNavButton(kIdNavStartupApps, L"Startup Apps", 0);
        navUsers_ = createNavButton(kIdNavUsers, L"Users", 0);
        navQuickTools_ = createNavButton(kIdNavQuickTools, L"Quick Kill Tools", 0);

        sectionTitle_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Processes",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdSectionTitle),
            instance_,
            nullptr);

        filterLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Search",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdFilterLabel),
            instance_,
            nullptr);

        filterEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            nullptr,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdFilterEdit),
            instance_,
            nullptr);

        processList_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            nullptr,
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdProcessList),
            instance_,
            nullptr);

        if (processList_)
        {
            SetWindowTheme(processList_, L"Explorer", nullptr);
            ListView_SetExtendedListViewStyle(
                processList_,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP | LVS_EX_LABELTIP);

            struct ColumnDef
            {
                const wchar_t *title;
                int width;
            };

            const ColumnDef columns[] = {
                {L"Name", 260},
                {L"PID", 78},
                {L"CPU %", 84},
                {L"Working Set", 126},
                {L"Private", 126},
                {L"Threads", 82},
                {L"Handles", 82},
                {L"Parent PID", 94},
                {L"Priority", 112},
                {L"Read Bytes", 126},
                {L"Write Bytes", 126}};

            for (int i = 0; i < static_cast<int>(sizeof(columns) / sizeof(columns[0])); ++i)
            {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.pszText = const_cast<LPWSTR>(columns[i].title);
                column.cx = columns[i].width;
                column.iSubItem = i;
                ListView_InsertColumn(processList_, i, &column);
            }
        }

        performancePlaceholder_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Live performance metrics",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdPerfPlaceholder),
            instance_,
            nullptr);

        auto createPerfGraph = [&](int id)
        {
            return CreateWindowExW(
                0,
                L"STATIC",
                nullptr,
                WS_CHILD | SS_OWNERDRAW,
                0,
                0,
                0,
                0,
                hwnd_,
                MenuId(id),
                instance_,
                nullptr);
        };

        perfGraphCpu_ = createPerfGraph(kIdPerfGraphCpu);
        perfGraphMemory_ = createPerfGraph(kIdPerfGraphMemory);
        perfGraphGpu_ = createPerfGraph(kIdPerfGraphGpu);
        perfGraphUpload_ = createPerfGraph(kIdPerfGraphUpload);
        perfGraphDownload_ = createPerfGraph(kIdPerfGraphDownload);

        perfCoreGrid_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"STATIC",
            nullptr,
            WS_CHILD | SS_OWNERDRAW,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdPerfCoreGrid),
            instance_,
            nullptr);

        networkPlaceholder_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Network activity and connection breakdowns are queued for the next iteration.\r\n\r\nThis section will include per-process throughput and port visibility.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdNetworkPlaceholder),
            instance_,
            nullptr);

        hardwarePlaceholder_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Hardware insights are staged next.\r\n\r\nPlanned content: storage devices, adapters, and peripheral status.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwarePlaceholder),
            instance_,
            nullptr);

        servicesPlaceholder_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Services view is wired into navigation.\r\n\r\nNext phase: list all services with state, startup type, and control actions.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdServicesPlaceholder),
            instance_,
            nullptr);

        startupAppsPlaceholder_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Startup Apps view is wired into navigation.\r\n\r\nNext phase: per-app startup impact and enable/disable actions.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdStartupAppsPlaceholder),
            instance_,
            nullptr);

        usersPlaceholder_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Users view is wired into navigation.\r\n\r\nNext phase: per-user CPU, memory, and process usage summary.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdUsersPlaceholder),
            instance_,
            nullptr);

        quickToolsTitle_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Quick Kill Tools",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolsTitle),
            instance_,
            nullptr);

        quickToolsHint_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Enter targets, then choose kill all or one-by-one confirmation mode.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolsHint),
            instance_,
            nullptr);

        quickPortLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Ports (e.g. 5000 3000 3690 6699)",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickPortLabel),
            instance_,
            nullptr);

        quickPortEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            nullptr,
            WS_CHILD | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickPortEdit),
            instance_,
            nullptr);

        quickKillPortButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Kill Ports (All Now)",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolPortKillAll),
            instance_,
            nullptr);

        quickPortKillOneButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Kill Ports (One By One)",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolPortKillOneByOne),
            instance_,
            nullptr);

        quickProcessLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Process Patterns (e.g. node ollama)",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickProcessLabel),
            instance_,
            nullptr);

        quickProcessEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            nullptr,
            WS_CHILD | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickProcessEdit),
            instance_,
            nullptr);

        quickKillPatternButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Kill Processes (All Now)",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolProcessKillAll),
            instance_,
            nullptr);

        quickProcessKillOneButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Kill Processes (One By One)",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolProcessKillOneByOne),
            instance_,
            nullptr);

        quickDeleteLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Force Delete Target (select with Explorer)",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickDeleteLabel),
            instance_,
            nullptr);

        quickDeletePathEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            nullptr,
            WS_CHILD | ES_AUTOHSCROLL | ES_READONLY,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickDeletePathEdit),
            instance_,
            nullptr);

        quickBrowseFileButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Browse File",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolBrowseFile),
            instance_,
            nullptr);

        quickBrowseFolderButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Browse Folder",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolBrowseFolder),
            instance_,
            nullptr);

        quickKillSmartDeleteButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Force Delete Selected",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolSmartDelete),
            instance_,
            nullptr);

        statusText_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Initializing...",
            WS_CHILD | WS_VISIBLE,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdStatus),
            instance_,
            nullptr);

        if (!sidebarTitle_ || !navProcesses_ || !navPerformance_ || !navNetwork_ || !navHardware_ || !navServices_ || !navStartupApps_ || !navUsers_ || !navQuickTools_ ||
            !sectionTitle_ || !filterLabel_ || !filterEdit_ || !processList_ ||
            !performancePlaceholder_ || !perfGraphCpu_ || !perfGraphMemory_ || !perfGraphGpu_ || !perfGraphUpload_ || !perfGraphDownload_ || !perfCoreGrid_ ||
            !networkPlaceholder_ || !hardwarePlaceholder_ || !servicesPlaceholder_ || !startupAppsPlaceholder_ || !usersPlaceholder_ ||
            !quickToolsTitle_ || !quickToolsHint_ ||
            !quickPortLabel_ || !quickPortEdit_ || !quickKillPortButton_ || !quickPortKillOneButton_ ||
            !quickProcessLabel_ || !quickProcessEdit_ || !quickKillPatternButton_ || !quickProcessKillOneButton_ ||
            !quickDeleteLabel_ || !quickDeletePathEdit_ || !quickBrowseFileButton_ || !quickBrowseFolderButton_ || !quickKillSmartDeleteButton_ ||
            !statusText_)
        {
            util::logging::Logger::Instance().Write(
                util::logging::LogLevel::Error,
                L"CreateChildControls failed. LastError=" + Win32ErrorToText(GetLastError()));
            return false;
        }

        applyFont(sidebarTitle_, uiTitleFont_);
        applyFont(navProcesses_, uiBoldFont_);
        applyFont(navPerformance_, uiBoldFont_);
        applyFont(navNetwork_, uiBoldFont_);
        applyFont(navHardware_, uiBoldFont_);
        applyFont(navServices_, uiBoldFont_);
        applyFont(navStartupApps_, uiBoldFont_);
        applyFont(navUsers_, uiBoldFont_);
        applyFont(navQuickTools_, uiBoldFont_);
        applyFont(sectionTitle_, uiTitleFont_);
        applyFont(filterLabel_, uiBoldFont_);
        applyFont(filterEdit_, uiFont_);
        applyFont(processList_, uiFont_);
        applyFont(performancePlaceholder_, uiFont_);
        applyFont(perfCoreGrid_, uiFont_);
        applyFont(networkPlaceholder_, uiFont_);
        applyFont(hardwarePlaceholder_, uiFont_);
        applyFont(servicesPlaceholder_, uiFont_);
        applyFont(startupAppsPlaceholder_, uiFont_);
        applyFont(usersPlaceholder_, uiFont_);
        applyFont(quickToolsTitle_, uiTitleFont_);
        applyFont(quickToolsHint_, uiFont_);
        applyFont(quickPortLabel_, uiBoldFont_);
        applyFont(quickPortEdit_, uiFont_);
        applyFont(quickKillPortButton_, uiBoldFont_);
        applyFont(quickPortKillOneButton_, uiBoldFont_);
        applyFont(quickProcessLabel_, uiBoldFont_);
        applyFont(quickProcessEdit_, uiFont_);
        applyFont(quickKillPatternButton_, uiBoldFont_);
        applyFont(quickProcessKillOneButton_, uiBoldFont_);
        applyFont(quickDeleteLabel_, uiBoldFont_);
        applyFont(quickDeletePathEdit_, uiFont_);
        applyFont(quickBrowseFileButton_, uiBoldFont_);
        applyFont(quickBrowseFolderButton_, uiBoldFont_);
        applyFont(quickKillSmartDeleteButton_, uiBoldFont_);
        applyFont(statusText_, uiFont_);

        UpdateSidebarSelection();
        ApplySectionVisibility();
        return true;
    }

    void MainWindow::LayoutControls()
    {
        RECT client{};
        GetClientRect(hwnd_, &client);

        const int width = (std::max)(300, static_cast<int>(client.right - client.left));
        const int height = (std::max)(220, static_cast<int>(client.bottom - client.top));

        MoveWindow(sidebar_, 0, 0, kSidebarWidth, height, TRUE);

        const int navX = 14;
        const int navWidth = kSidebarWidth - (2 * navX);
        int navY = 66;

        MoveWindow(sidebarTitle_, navX, 16, navWidth, 36, TRUE);
        MoveWindow(navProcesses_, navX, navY, navWidth, kNavButtonHeight, TRUE);
        navY += kNavButtonHeight + 6;
        MoveWindow(navPerformance_, navX, navY, navWidth, kNavButtonHeight, TRUE);
        navY += kNavButtonHeight + 6;
        MoveWindow(navNetwork_, navX, navY, navWidth, kNavButtonHeight, TRUE);
        navY += kNavButtonHeight + 6;
        MoveWindow(navHardware_, navX, navY, navWidth, kNavButtonHeight, TRUE);
        navY += kNavButtonHeight + 6;
        MoveWindow(navServices_, navX, navY, navWidth, kNavButtonHeight, TRUE);
        navY += kNavButtonHeight + 6;
        MoveWindow(navStartupApps_, navX, navY, navWidth, kNavButtonHeight, TRUE);
        navY += kNavButtonHeight + 6;
        MoveWindow(navUsers_, navX, navY, navWidth, kNavButtonHeight, TRUE);
        navY += kNavButtonHeight + 14;
        MoveWindow(navQuickTools_, navX, navY, navWidth, kNavButtonHeight, TRUE);

        const int contentX = kSidebarWidth + 18;
        const int contentWidth = (std::max)(120, width - contentX - 18);
        const int statusY = (std::max)(140, height - kStatusHeight - kPadding);

        MoveWindow(sectionTitle_, contentX, kPadding, contentWidth, 36, TRUE);
        MoveWindow(statusText_, contentX, statusY, contentWidth, kStatusHeight, TRUE);

        const int bodyTop = kPadding + 44;
        const int bodyHeight = (std::max)(40, statusY - bodyTop - kPadding);

        MoveWindow(filterLabel_, contentX, bodyTop + 3, 70, kFilterHeight, TRUE);
        MoveWindow(filterEdit_, contentX + 70, bodyTop, (std::max)(80, contentWidth - 70), kFilterHeight + 4, TRUE);

        const int listTop = bodyTop + kFilterHeight + kPadding;
        const int listHeight = (std::max)(40, statusY - listTop - kPadding);
        MoveWindow(processList_, contentX, listTop, contentWidth, listHeight, TRUE);

        MoveWindow(performancePlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);
        MoveWindow(networkPlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);
        MoveWindow(hardwarePlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);
        MoveWindow(servicesPlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);
        MoveWindow(startupAppsPlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);
        MoveWindow(usersPlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);

        const int perfGap = 8;
        const int perfGraphHeight = 102;
        const int perfHalfWidth = (contentWidth - perfGap) / 2;
        int perfY = bodyTop;

        MoveWindow(performancePlaceholder_, contentX, perfY, contentWidth, 22, TRUE);
        perfY += 26;

        MoveWindow(perfGraphCpu_, contentX, perfY, perfHalfWidth, perfGraphHeight, TRUE);
        MoveWindow(perfGraphMemory_, contentX + perfHalfWidth + perfGap, perfY, contentWidth - perfHalfWidth - perfGap, perfGraphHeight, TRUE);
        perfY += perfGraphHeight + perfGap;

        MoveWindow(perfGraphUpload_, contentX, perfY, perfHalfWidth, perfGraphHeight, TRUE);
        MoveWindow(perfGraphDownload_, contentX + perfHalfWidth + perfGap, perfY, contentWidth - perfHalfWidth - perfGap, perfGraphHeight, TRUE);
        perfY += perfGraphHeight + perfGap;

        MoveWindow(perfGraphGpu_, contentX, perfY, contentWidth, perfGraphHeight, TRUE);
        perfY += perfGraphHeight + perfGap;

        const int coreGridHeight = (std::max)(80, statusY - perfY - kPadding);
        MoveWindow(perfCoreGrid_, contentX, perfY, contentWidth, coreGridHeight, TRUE);

        int quickY = bodyTop + 4;
        MoveWindow(quickToolsTitle_, contentX, quickY, contentWidth, 32, TRUE);
        quickY += 34;
        MoveWindow(quickToolsHint_, contentX, quickY, contentWidth, 24, TRUE);
        quickY += 30;

        MoveWindow(quickPortLabel_, contentX, quickY, contentWidth, 22, TRUE);
        quickY += 24;
        MoveWindow(quickPortEdit_, contentX, quickY, contentWidth, 30, TRUE);
        quickY += 36;
        const int halfButtonsWidth = (contentWidth - 10) / 2;
        MoveWindow(quickKillPortButton_, contentX, quickY, halfButtonsWidth, 32, TRUE);
        MoveWindow(quickPortKillOneButton_, contentX + halfButtonsWidth + 10, quickY, contentWidth - halfButtonsWidth - 10, 32, TRUE);
        quickY += 40;

        MoveWindow(quickProcessLabel_, contentX, quickY, contentWidth, 22, TRUE);
        quickY += 24;
        MoveWindow(quickProcessEdit_, contentX, quickY, contentWidth, 30, TRUE);
        quickY += 36;
        MoveWindow(quickKillPatternButton_, contentX, quickY, halfButtonsWidth, 32, TRUE);
        MoveWindow(quickProcessKillOneButton_, contentX + halfButtonsWidth + 10, quickY, contentWidth - halfButtonsWidth - 10, 32, TRUE);
        quickY += 40;

        MoveWindow(quickDeleteLabel_, contentX, quickY, contentWidth, 22, TRUE);
        quickY += 24;
        MoveWindow(quickDeletePathEdit_, contentX, quickY, contentWidth, 30, TRUE);
        quickY += 36;
        const int thirdButtonsWidth = (contentWidth - 20) / 3;
        MoveWindow(quickBrowseFileButton_, contentX, quickY, thirdButtonsWidth, 32, TRUE);
        MoveWindow(quickBrowseFolderButton_, contentX + thirdButtonsWidth + 10, quickY, thirdButtonsWidth, 32, TRUE);
        MoveWindow(quickKillSmartDeleteButton_, contentX + (thirdButtonsWidth + 10) * 2, quickY, contentWidth - (thirdButtonsWidth + 10) * 2, 32, TRUE);

        SetWindowTextW(sectionTitle_, SectionTitleText().c_str());
        SetWindowTextW(filterLabel_, L"Search");
        ApplySectionVisibility();
    }

    void MainWindow::ApplySectionVisibility()
    {
        const bool processTab = activeSection_ == Section::Processes;
        const bool perfTab = activeSection_ == Section::Performance;
        const bool networkTab = activeSection_ == Section::Network;
        const bool hardwareTab = activeSection_ == Section::Hardware;
        const bool servicesTab = activeSection_ == Section::Services;
        const bool startupTab = activeSection_ == Section::StartupApps;
        const bool usersTab = activeSection_ == Section::Users;
        const bool quickToolsTab = activeSection_ == Section::QuickKillTools;

        const bool showFilter = processTab;
        ShowWindow(filterLabel_, showFilter ? SW_SHOW : SW_HIDE);
        ShowWindow(filterEdit_, showFilter ? SW_SHOW : SW_HIDE);
        ShowWindow(processList_, processTab ? SW_SHOW : SW_HIDE);

        ShowWindow(performancePlaceholder_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphCpu_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphMemory_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphGpu_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphUpload_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphDownload_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfCoreGrid_, perfTab ? SW_SHOW : SW_HIDE);

        ShowWindow(networkPlaceholder_, networkTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwarePlaceholder_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(servicesPlaceholder_, servicesTab ? SW_SHOW : SW_HIDE);
        ShowWindow(startupAppsPlaceholder_, startupTab ? SW_SHOW : SW_HIDE);
        ShowWindow(usersPlaceholder_, usersTab ? SW_SHOW : SW_HIDE);

        ShowWindow(quickToolsTitle_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickToolsHint_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickPortLabel_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickPortEdit_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickKillPortButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickPortKillOneButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickProcessLabel_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickProcessEdit_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickKillPatternButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickProcessKillOneButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickDeleteLabel_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickDeletePathEdit_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickBrowseFileButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickBrowseFolderButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickKillSmartDeleteButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
    }

    void MainWindow::SetActiveSection(Section section)
    {
        if (activeSection_ == section)
        {
            return;
        }

        activeSection_ = section;
        UpdateSidebarSelection();
        LayoutControls();

        if (activeSection_ == Section::Processes)
        {
            RefreshProcessView();
        }
    }

    void MainWindow::UpdateSidebarSelection()
    {
        int checked = kIdNavProcesses;
        switch (activeSection_)
        {
        case Section::Processes:
            checked = kIdNavProcesses;
            break;
        case Section::Performance:
            checked = kIdNavPerformance;
            break;
        case Section::Network:
            checked = kIdNavNetwork;
            break;
        case Section::Hardware:
            checked = kIdNavHardware;
            break;
        case Section::Services:
            checked = kIdNavServices;
            break;
        case Section::StartupApps:
            checked = kIdNavStartupApps;
            break;
        case Section::Users:
            checked = kIdNavUsers;
            break;
        case Section::QuickKillTools:
            checked = kIdNavQuickTools;
            break;
        }

        CheckRadioButton(hwnd_, kIdNavProcesses, kIdNavQuickTools, checked);
    }

    std::wstring MainWindow::SectionTitleText() const
    {
        switch (activeSection_)
        {
        case Section::Processes:
            return L"Processes";
        case Section::Performance:
            return L"Performance";
        case Section::Network:
            return L"Network";
        case Section::Hardware:
            return L"Hardware";
        case Section::Services:
            return L"Services";
        case Section::StartupApps:
            return L"Startup Apps";
        case Section::Users:
            return L"Users";
        case Section::QuickKillTools:
            return L"Quick Kill Tools";
        default:
            return L"Ultimate Task Manager";
        }
    }

    void MainWindow::PushHistory(std::deque<double> &history, double value, size_t maxSamples)
    {
        history.push_back(value);
        while (history.size() > maxSamples)
        {
            history.pop_front();
        }
    }

    double MainWindow::QueryGpuUsagePercent()
    {
        if (!gpuCounterReady_ || !gpuQuery_ || !gpuCounter_)
        {
            return 0.0;
        }

        if (PdhCollectQueryData(gpuQuery_) != ERROR_SUCCESS)
        {
            return 0.0;
        }

        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        PDH_STATUS status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        if (status != ERROR_SUCCESS && bufferSize == 0)
        {
            return 0.0;
        }

        if (bufferSize == 0 || itemCount == 0)
        {
            return 0.0;
        }

        std::vector<std::uint8_t> buffer(bufferSize);
        auto *items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
        status = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
        if (status != ERROR_SUCCESS)
        {
            return 0.0;
        }

        double total = 0.0;
        for (DWORD i = 0; i < itemCount; ++i)
        {
            if (items[i].FmtValue.CStatus == ERROR_SUCCESS && items[i].FmtValue.doubleValue > 0.0)
            {
                total += items[i].FmtValue.doubleValue;
            }
        }

        return (std::clamp)(total, 0.0, 100.0);
    }

    void MainWindow::UpdatePerformanceMetrics()
    {
        const DWORD detectedCoreCount = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        const DWORD coreCount = detectedCoreCount > 0 ? detectedCoreCount : 1;

        std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> cpuPerf(coreCount);
        ULONG returned = 0;
        const NTSTATUS status = utm::system::ntapi::NtApi::Instance().QuerySystemInformation(
            SystemProcessorPerformanceInformation,
            cpuPerf.data(),
            static_cast<ULONG>(cpuPerf.size() * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)),
            &returned);

        if (NT_SUCCESS(status))
        {
            if (performanceCoreUsage_.size() != coreCount)
            {
                performanceCoreUsage_.assign(coreCount, 0.0);
                performanceCoreHistory_.assign(coreCount, {});
                previousCpuIdle_.assign(coreCount, 0);
                previousCpuTotal_.assign(coreCount, 0);
                cpuSamplingReady_ = false;
            }

            double total = 0.0;
            for (DWORD i = 0; i < coreCount; ++i)
            {
                const std::uint64_t idle = static_cast<std::uint64_t>(cpuPerf[i].IdleTime.QuadPart);
                const std::uint64_t totalTime = static_cast<std::uint64_t>(cpuPerf[i].KernelTime.QuadPart + cpuPerf[i].UserTime.QuadPart);

                double coreUsage = 0.0;
                if (cpuSamplingReady_)
                {
                    const std::uint64_t idleDelta = idle >= previousCpuIdle_[i] ? idle - previousCpuIdle_[i] : 0;
                    const std::uint64_t totalDelta = totalTime >= previousCpuTotal_[i] ? totalTime - previousCpuTotal_[i] : 0;

                    if (totalDelta > 0)
                    {
                        const std::uint64_t busyDelta = totalDelta > idleDelta ? totalDelta - idleDelta : 0;
                        coreUsage = (static_cast<double>(busyDelta) * 100.0) / static_cast<double>(totalDelta);
                    }
                }

                coreUsage = (std::clamp)(coreUsage, 0.0, 100.0);
                performanceCoreUsage_[i] = coreUsage;
                PushHistory(performanceCoreHistory_[i], coreUsage);
                previousCpuIdle_[i] = idle;
                previousCpuTotal_[i] = totalTime;
                total += coreUsage;
            }

            cpuSamplingReady_ = true;
            totalCpuPercent_ = coreCount > 0 ? total / coreCount : 0.0;
        }

        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem))
        {
            memoryTotalGb_ = static_cast<double>(mem.ullTotalPhys) / kBytesPerGiB;
            memoryUsedGb_ = static_cast<double>(mem.ullTotalPhys - mem.ullAvailPhys) / kBytesPerGiB;

            if (memoryTotalGb_ > 0.0)
            {
                memoryPercent_ = (memoryUsedGb_ * 100.0) / memoryTotalGb_;
                memoryUsedGb_ = (std::clamp)(memoryUsedGb_, 0.0, memoryTotalGb_);
            }
            else
            {
                memoryTotalGb_ = 1.0;
                memoryUsedGb_ = 0.0;
                memoryPercent_ = static_cast<double>(mem.dwMemoryLoad);
            }

            memoryPercent_ = (std::clamp)(memoryPercent_, 0.0, 100.0);
        }

        ULONG ifTableSize = 0;
        if (GetIfTable(nullptr, &ifTableSize, FALSE) == ERROR_INSUFFICIENT_BUFFER && ifTableSize > 0)
        {
            std::vector<std::uint8_t> ifBuffer(ifTableSize);
            auto *ifTable = reinterpret_cast<MIB_IFTABLE *>(ifBuffer.data());
            if (GetIfTable(ifTable, &ifTableSize, FALSE) == NO_ERROR)
            {
                std::uint64_t totalIn = 0;
                std::uint64_t totalOut = 0;

                for (DWORD i = 0; i < ifTable->dwNumEntries; ++i)
                {
                    const auto &row = ifTable->table[i];
                    if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK || row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL)
                    {
                        continue;
                    }

                    totalIn += row.dwInOctets;
                    totalOut += row.dwOutOctets;
                }

                const std::uint64_t nowMs = GetTickCount64();
                if (networkSamplingReady_ && nowMs > previousNetworkTickMs_)
                {
                    const double elapsedSec = static_cast<double>(nowMs - previousNetworkTickMs_) / 1000.0;
                    if (elapsedSec > 0.0001)
                    {
                        const std::uint64_t inDelta = totalIn >= previousNetworkIn_ ? totalIn - previousNetworkIn_ : 0;
                        const std::uint64_t outDelta = totalOut >= previousNetworkOut_ ? totalOut - previousNetworkOut_ : 0;

                        downloadMbps_ = (static_cast<double>(inDelta) * 8.0) / (elapsedSec * 1000.0 * 1000.0);
                        uploadMbps_ = (static_cast<double>(outDelta) * 8.0) / (elapsedSec * 1000.0 * 1000.0);
                    }
                }

                previousNetworkIn_ = totalIn;
                previousNetworkOut_ = totalOut;
                previousNetworkTickMs_ = nowMs;
                networkSamplingReady_ = true;
            }
        }

        gpuPercent_ = QueryGpuUsagePercent();

        PushHistory(cpuHistory_, totalCpuPercent_);
        PushHistory(memoryHistory_, memoryUsedGb_);
        PushHistory(gpuHistory_, gpuPercent_);
        PushHistory(uploadHistory_, uploadMbps_);
        PushHistory(downloadHistory_, downloadMbps_);
    }

    void MainWindow::RefreshPerformancePanel()
    {
        if (!perfCoreGrid_)
        {
            return;
        }

        auto updateDynamicScale = [](double &scale, const std::deque<double> &history)
        {
            double peak = 0.0;
            for (double value : history)
            {
                if (value > peak)
                {
                    peak = value;
                }
            }

            const double floor = 0.5;
            double target = peak > 0.0 ? peak * 1.25 : floor;
            target = (std::max)(floor, target);

            if (scale <= 0.0)
            {
                scale = target;
                return;
            }

            if (target > scale)
            {
                scale = target;
            }
            else
            {
                scale = scale * 0.88 + target * 0.12;
            }

            scale = (std::max)(floor, scale);
        };

        updateDynamicScale(uploadScaleMbps_, uploadHistory_);
        updateDynamicScale(downloadScaleMbps_, downloadHistory_);

        std::wstringstream caption;
        caption << L"Live performance | CPU " << static_cast<int>(totalCpuPercent_ + 0.5)
                << L"% | Memory " << FormatNumber(memoryUsedGb_, memoryTotalGb_ < 10.0 ? 1 : 0)
                << L" / " << FormatNumber(memoryTotalGb_, memoryTotalGb_ < 10.0 ? 1 : 0)
                << L" GB (" << static_cast<int>(memoryPercent_ + 0.5)
                << L"%) | GPU " << static_cast<int>(gpuPercent_ + 0.5)
                << L"% | Up " << FormatNumber(uploadMbps_, uploadMbps_ < 10.0 ? 1 : 0)
                << L" Mbps | Down " << FormatNumber(downloadMbps_, downloadMbps_ < 10.0 ? 1 : 0) << L" Mbps";
        SetWindowTextW(performancePlaceholder_, caption.str().c_str());

        if (perfGraphCpu_)
            InvalidateRect(perfGraphCpu_, nullptr, FALSE);
        if (perfGraphMemory_)
            InvalidateRect(perfGraphMemory_, nullptr, FALSE);
        if (perfGraphGpu_)
            InvalidateRect(perfGraphGpu_, nullptr, FALSE);
        if (perfGraphUpload_)
            InvalidateRect(perfGraphUpload_, nullptr, FALSE);
        if (perfGraphDownload_)
            InvalidateRect(perfGraphDownload_, nullptr, FALSE);
        if (perfCoreGrid_)
            InvalidateRect(perfCoreGrid_, nullptr, FALSE);
    }

    void MainWindow::DrawPerformanceGraph(const DRAWITEMSTRUCT *draw) const
    {
        if (!draw)
        {
            return;
        }

        struct GraphConfig
        {
            const std::deque<double> *history = nullptr;
            const wchar_t *title = L"";
            const wchar_t *unit = L"";
            COLORREF lineColor = RGB(37, 99, 235);
            double latest = 0.0;
            double maxValue = 100.0;
            bool showPercent = false;
            bool showCoreOverlay = false;
        };

        GraphConfig cfg{};
        switch (draw->CtlID)
        {
        case kIdPerfGraphCpu:
            cfg.history = &cpuHistory_;
            cfg.title = L"CPU Total";
            cfg.unit = L"%";
            cfg.lineColor = RGB(30, 120, 230);
            cfg.latest = totalCpuPercent_;
            cfg.maxValue = 100.0;
            cfg.showPercent = true;
            break;
        case kIdPerfGraphMemory:
            cfg.history = &memoryHistory_;
            cfg.title = L"Memory";
            cfg.unit = L"GB";
            cfg.lineColor = RGB(16, 166, 115);
            cfg.latest = memoryUsedGb_;
            cfg.maxValue = (std::max)(1.0, memoryTotalGb_);
            break;
        case kIdPerfGraphGpu:
            cfg.history = &gpuHistory_;
            cfg.title = L"GPU";
            cfg.unit = L"%";
            cfg.lineColor = RGB(148, 73, 224);
            cfg.latest = gpuPercent_;
            cfg.maxValue = 100.0;
            cfg.showPercent = true;
            break;
        case kIdPerfGraphUpload:
            cfg.history = &uploadHistory_;
            cfg.title = L"Network Upload";
            cfg.unit = L"Mbps";
            cfg.lineColor = RGB(236, 150, 14);
            cfg.latest = uploadMbps_;
            cfg.maxValue = (std::max)(0.5, uploadScaleMbps_);
            break;
        case kIdPerfGraphDownload:
            cfg.history = &downloadHistory_;
            cfg.title = L"Network Download";
            cfg.unit = L"Mbps";
            cfg.lineColor = RGB(225, 76, 88);
            cfg.latest = downloadMbps_;
            cfg.maxValue = (std::max)(0.5, downloadScaleMbps_);
            break;
        default:
            return;
        }

        if (!cfg.history)
        {
            return;
        }

        HDC targetDc = draw->hDC;
        RECT rc = draw->rcItem;
        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;
        if (width <= 2 || height <= 2)
        {
            return;
        }

        HDC dc = CreateCompatibleDC(targetDc);
        HBITMAP bitmap = CreateCompatibleBitmap(targetDc, width, height);
        HBITMAP oldBitmap = reinterpret_cast<HBITMAP>(SelectObject(dc, bitmap));

        RECT local{0, 0, width, height};
        FillRect(dc, &local, cardBrush_);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(204, 214, 229));
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(dc, borderPen));
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, local.left, local.top, local.right, local.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(30, 46, 70));

        RECT titleRect = local;
        titleRect.left += 10;
        titleRect.top += 5;
        DrawTextW(dc, cfg.title, -1, &titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

        std::wstring latestLabel;
        if (draw->CtlID == kIdPerfGraphMemory)
        {
            latestLabel = FormatAxisValue(cfg.latest, cfg.unit);
            latestLabel += L" (";
            latestLabel += std::to_wstring(static_cast<int>(memoryPercent_ + 0.5));
            latestLabel += L"%)";
        }
        else if (cfg.showPercent)
        {
            latestLabel = std::to_wstring(static_cast<int>(cfg.latest + 0.5));
            latestLabel += L"%";
        }
        else
        {
            latestLabel = FormatAxisValue(cfg.latest, cfg.unit);
        }

        RECT valueRect = local;
        valueRect.right -= 10;
        valueRect.top += 5;
        DrawTextW(dc, latestLabel.c_str(), -1, &valueRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);

        RECT plot = local;
        plot.left += 46;
        plot.right -= 10;
        plot.top += 28;
        plot.bottom -= 20;

        const int plotWidth = (std::max)(1, static_cast<int>(plot.right - plot.left));
        const int plotHeight = (std::max)(1, static_cast<int>(plot.bottom - plot.top));

        HBRUSH plotBrush = CreateSolidBrush(BlendColor(RGB(255, 255, 255), cfg.lineColor, 0.9));
        FillRect(dc, &plot, plotBrush);
        DeleteObject(plotBrush);

        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(221, 229, 240));
        oldPen = reinterpret_cast<HPEN>(SelectObject(dc, gridPen));

        for (int i = 0; i <= 4; ++i)
        {
            const int y = plot.top + ((plotHeight - 1) * i) / 4;
            MoveToEx(dc, plot.left, y, nullptr);
            LineTo(dc, plot.right, y);

            const double tickValue = cfg.maxValue * static_cast<double>(4 - i) / 4.0;
            std::wstring yLabel;
            if (cfg.showPercent)
            {
                yLabel = std::to_wstring(static_cast<int>(tickValue + 0.5));
                yLabel += L"%";
            }
            else
            {
                yLabel = FormatAxisValue(tickValue, cfg.unit);
            }

            RECT yRect{2, y - 9, plot.left - 6, y + 9};
            SetTextColor(dc, RGB(94, 106, 128));
            DrawTextW(dc, yLabel.c_str(), -1, &yRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }

        for (int i = 1; i <= 5; ++i)
        {
            const int x = plot.left + ((plotWidth - 1) * i) / 6;
            MoveToEx(dc, x, plot.top, nullptr);
            LineTo(dc, x, plot.bottom);
        }

        SelectObject(dc, oldPen);
        DeleteObject(gridPen);

        auto drawSeries = [&](const std::deque<double> &series, COLORREF color, int thickness, bool fillArea)
        {
            if (series.size() < 2)
            {
                return;
            }

            const int n = static_cast<int>(series.size());
            std::vector<POINT> points;
            points.reserve(n);

            for (int i = 0; i < n; ++i)
            {
                const double clamped = (std::clamp)(series[i], 0.0, cfg.maxValue);
                const int x = plot.left + ((plotWidth - 1) * i) / (n - 1);
                const int y = plot.bottom - static_cast<int>((clamped / cfg.maxValue) * (plotHeight - 1));
                points.push_back(POINT{x, y});
            }

            if (fillArea)
            {
                std::vector<POINT> area;
                area.reserve(points.size() + 2);
                area.push_back(POINT{points.front().x, plot.bottom});
                area.insert(area.end(), points.begin(), points.end());
                area.push_back(POINT{points.back().x, plot.bottom});

                HBRUSH areaBrush = CreateSolidBrush(BlendColor(color, RGB(255, 255, 255), 0.75));
                HBRUSH prevBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, areaBrush));
                HPEN nullPen = reinterpret_cast<HPEN>(SelectObject(dc, GetStockObject(NULL_PEN)));
                Polygon(dc, area.data(), static_cast<int>(area.size()));
                SelectObject(dc, nullPen);
                SelectObject(dc, prevBrush);
                DeleteObject(areaBrush);
            }

            HPEN linePen = CreatePen(PS_SOLID, thickness, color);
            HPEN prevPen = reinterpret_cast<HPEN>(SelectObject(dc, linePen));
            MoveToEx(dc, points.front().x, points.front().y, nullptr);
            for (size_t i = 1; i < points.size(); ++i)
            {
                LineTo(dc, points[i].x, points[i].y);
            }
            SelectObject(dc, prevPen);
            DeleteObject(linePen);
        };

        if (cfg.showCoreOverlay && !performanceCoreHistory_.empty())
        {
            constexpr COLORREF corePalette[] = {
                RGB(99, 171, 255), RGB(92, 214, 189), RGB(119, 138, 252), RGB(239, 167, 96),
                RGB(131, 227, 117), RGB(231, 113, 135), RGB(85, 196, 223), RGB(188, 160, 255)};

            const size_t maxSeries = 24;
            const size_t coreCount = performanceCoreHistory_.size();
            const size_t step = coreCount > maxSeries ? (coreCount + maxSeries - 1) / maxSeries : 1;

            for (size_t core = 0; core < coreCount; core += step)
            {
                const COLORREF base = corePalette[(core / step) % (sizeof(corePalette) / sizeof(corePalette[0]))];
                const COLORREF line = BlendColor(base, RGB(255, 255, 255), 0.30);
                drawSeries(performanceCoreHistory_[core], line, 1, false);
            }
        }

        drawSeries(*cfg.history, cfg.lineColor, 2, true);

        if (!cfg.history->empty())
        {
            const int n = static_cast<int>(cfg.history->size());
            const double tail = (std::clamp)(cfg.history->back(), 0.0, cfg.maxValue);
            const int tailX = plot.left + ((plotWidth - 1) * (n - 1)) / (n > 1 ? (n - 1) : 1);
            const int tailY = plot.bottom - static_cast<int>((tail / cfg.maxValue) * (plotHeight - 1));

            HBRUSH markerBrush = CreateSolidBrush(cfg.lineColor);
            HBRUSH prevBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, markerBrush));
            HPEN markerPen = CreatePen(PS_SOLID, 1, BlendColor(cfg.lineColor, RGB(0, 0, 0), 0.2));
            HPEN prevPen = reinterpret_cast<HPEN>(SelectObject(dc, markerPen));
            Ellipse(dc, tailX - 3, tailY - 3, tailX + 4, tailY + 4);
            SelectObject(dc, prevPen);
            SelectObject(dc, prevBrush);
            DeleteObject(markerPen);
            DeleteObject(markerBrush);
        }

        RECT xLabelRect = plot;
        xLabelRect.top = plot.bottom + 2;
        xLabelRect.bottom = local.bottom - 2;
        SetTextColor(dc, RGB(108, 120, 142));
        DrawTextW(dc, L"60s history", -1, &xLabelRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
        DrawTextW(dc, L"now", -1, &xLabelRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);

        BitBlt(targetDc, rc.left, rc.top, width, height, dc, 0, 0, SRCCOPY);

        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    void MainWindow::DrawCoreGrid(const DRAWITEMSTRUCT *draw) const
    {
        if (!draw)
        {
            return;
        }

        HDC targetDc = draw->hDC;
        RECT rc = draw->rcItem;
        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;
        if (width <= 2 || height <= 2)
        {
            return;
        }

        HDC dc = CreateCompatibleDC(targetDc);
        HBITMAP bitmap = CreateCompatibleBitmap(targetDc, width, height);
        HBITMAP oldBitmap = reinterpret_cast<HBITMAP>(SelectObject(dc, bitmap));

        RECT local{0, 0, width, height};
        FillRect(dc, &local, cardBrush_);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(204, 214, 229));
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(dc, borderPen));
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, local.left, local.top, local.right, local.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(borderPen);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(30, 46, 70));

        RECT titleRect = local;
        titleRect.left += 10;
        titleRect.top += 5;
        DrawTextW(dc, L"Per-Core CPU Graphs", -1, &titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

        const size_t coreCount = performanceCoreHistory_.size();
        if (coreCount == 0)
        {
            RECT waitRect = local;
            waitRect.left += 10;
            waitRect.top += 26;
            SetTextColor(dc, RGB(104, 118, 141));
            DrawTextW(dc, L"Collecting core samples...", -1, &waitRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

            BitBlt(targetDc, rc.left, rc.top, width, height, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(dc);
            return;
        }

        RECT grid = local;
        grid.left += 8;
        grid.right -= 8;
        grid.top += 26;
        grid.bottom -= 8;

        const int gap = 6;
        const int gridWidth = (std::max)(1, static_cast<int>(grid.right - grid.left));
        const int gridHeight = (std::max)(1, static_cast<int>(grid.bottom - grid.top));

        int columns = (std::max)(1, gridWidth / 150);
        columns = (std::min)(columns, static_cast<int>(coreCount));
        int rows = static_cast<int>((coreCount + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));

        while (rows > 1)
        {
            const int nextColumns = columns + 1;
            if (nextColumns > static_cast<int>(coreCount))
            {
                break;
            }

            const int candidateTileWidth = (gridWidth - gap * (nextColumns - 1)) / nextColumns;
            if (candidateTileWidth < 96)
            {
                break;
            }

            const int candidateRows = static_cast<int>((coreCount + static_cast<size_t>(nextColumns) - 1) / static_cast<size_t>(nextColumns));
            const int candidateTileHeight = (gridHeight - gap * (candidateRows - 1)) / candidateRows;
            if (candidateTileHeight < 50)
            {
                break;
            }

            columns = nextColumns;
            rows = candidateRows;
        }

        const int tileWidth = (std::max)(96, (gridWidth - gap * (columns - 1)) / columns);
        const int tileHeight = (std::max)(50, (gridHeight - gap * (rows - 1)) / rows);

        constexpr COLORREF kCorePalette[] = {
            RGB(68, 128, 245), RGB(25, 173, 123), RGB(130, 102, 246), RGB(229, 140, 28),
            RGB(210, 84, 109), RGB(23, 163, 203), RGB(163, 119, 230), RGB(94, 138, 40)};

        for (size_t idx = 0; idx < coreCount; ++idx)
        {
            const int row = static_cast<int>(idx / static_cast<size_t>(columns));
            const int col = static_cast<int>(idx % static_cast<size_t>(columns));

            RECT tile{
                grid.left + col * (tileWidth + gap),
                grid.top + row * (tileHeight + gap),
                grid.left + col * (tileWidth + gap) + tileWidth,
                grid.top + row * (tileHeight + gap) + tileHeight};

            if (tile.top >= grid.bottom)
            {
                break;
            }
            if (tile.right > grid.right)
            {
                tile.right = grid.right;
            }
            if (tile.bottom > grid.bottom)
            {
                tile.bottom = grid.bottom;
            }

            HBRUSH tileBrush = CreateSolidBrush(RGB(247, 250, 255));
            FillRect(dc, &tile, tileBrush);
            DeleteObject(tileBrush);

            HPEN tileBorder = CreatePen(PS_SOLID, 1, RGB(216, 225, 239));
            oldPen = reinterpret_cast<HPEN>(SelectObject(dc, tileBorder));
            oldBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
            Rectangle(dc, tile.left, tile.top, tile.right, tile.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(tileBorder);

            RECT nameRect = tile;
            nameRect.left += 5;
            nameRect.top += 3;
            std::wstring coreLabel = L"Core ";
            coreLabel += std::to_wstring(idx);
            SetTextColor(dc, RGB(53, 68, 94));
            DrawTextW(dc, coreLabel.c_str(), -1, &nameRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

            RECT usageRect = tile;
            usageRect.right -= 5;
            usageRect.top += 3;
            double usageValue = 0.0;
            if (idx < performanceCoreUsage_.size())
            {
                usageValue = performanceCoreUsage_[idx];
            }
            std::wstring usageText = std::to_wstring(static_cast<int>(usageValue + 0.5));
            usageText += L"%";
            DrawTextW(dc, usageText.c_str(), -1, &usageRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);

            RECT chart = tile;
            chart.left += 4;
            chart.right -= 4;
            chart.top += 18;
            chart.bottom -= 4;

            const int chartWidth = (std::max)(1, static_cast<int>(chart.right - chart.left));
            const int chartHeight = (std::max)(1, static_cast<int>(chart.bottom - chart.top));

            HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(228, 234, 245));
            oldPen = reinterpret_cast<HPEN>(SelectObject(dc, gridPen));
            for (int line = 1; line <= 2; ++line)
            {
                const int y = chart.top + (chartHeight * line) / 3;
                MoveToEx(dc, chart.left, y, nullptr);
                LineTo(dc, chart.right, y);
            }
            SelectObject(dc, oldPen);
            DeleteObject(gridPen);

            const auto &series = performanceCoreHistory_[idx];
            if (series.size() >= 2)
            {
                const COLORREF baseColor = kCorePalette[idx % (sizeof(kCorePalette) / sizeof(kCorePalette[0]))];

                std::vector<POINT> points;
                const int n = static_cast<int>(series.size());
                points.reserve(n);
                for (int i = 0; i < n; ++i)
                {
                    const double sample = (std::clamp)(series[i], 0.0, 100.0);
                    const int x = chart.left + ((chartWidth - 1) * i) / (n - 1);
                    const int y = chart.bottom - static_cast<int>((sample / 100.0) * (chartHeight - 1));
                    points.push_back(POINT{x, y});
                }

                std::vector<POINT> area;
                area.reserve(points.size() + 2);
                area.push_back(POINT{points.front().x, chart.bottom});
                area.insert(area.end(), points.begin(), points.end());
                area.push_back(POINT{points.back().x, chart.bottom});

                HBRUSH areaBrush = CreateSolidBrush(BlendColor(baseColor, RGB(255, 255, 255), 0.82));
                HBRUSH prevAreaBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, areaBrush));
                HPEN nullPen = reinterpret_cast<HPEN>(SelectObject(dc, GetStockObject(NULL_PEN)));
                Polygon(dc, area.data(), static_cast<int>(area.size()));
                SelectObject(dc, nullPen);
                SelectObject(dc, prevAreaBrush);
                DeleteObject(areaBrush);

                HPEN linePen = CreatePen(PS_SOLID, 1, baseColor);
                HPEN prevPen = reinterpret_cast<HPEN>(SelectObject(dc, linePen));
                MoveToEx(dc, points.front().x, points.front().y, nullptr);
                for (size_t p = 1; p < points.size(); ++p)
                {
                    LineTo(dc, points[p].x, points[p].y);
                }
                SelectObject(dc, prevPen);
                DeleteObject(linePen);
            }
        }

        BitBlt(targetDc, rc.left, rc.top, width, height, dc, 0, 0, SRCCOPY);

        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    void MainWindow::HandleSnapshotUpdate()
    {
        snapshot_ = engine_.GetSnapshot();
        ApplyFilterAndSort();
        UpdatePerformanceMetrics();
        RefreshPerformancePanel();

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
                pass = ContainsInsensitive(p.imageName, filterText_);
                if (!pass)
                {
                    const std::wstring pidText = std::to_wstring(p.pid);
                    pass = ContainsInsensitive(pidText, filterText_);
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
                result = compare(ToLower(left.imageName), ToLower(right.imageName));
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

    void MainWindow::ShowStatusText(const std::wstring &text)
    {
        if (statusText_)
        {
            SetWindowTextW(statusText_, text.c_str());
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
