#include "ui/MainWindow.h"

#include "tools/process/ProcessActions.h"
#include "util/logging/Logger.h"

#include <commctrl.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <sstream>

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
    constexpr int kIdNavQuickTools = 1014;

    constexpr int kIdSectionTitle = 1020;
    constexpr int kIdFilterLabel = 1021;
    constexpr int kIdFilterEdit = 1022;
    constexpr int kIdProcessList = 1023;
    constexpr int kIdStatus = 1024;
    constexpr int kIdPerfPlaceholder = 1025;
    constexpr int kIdNetworkPlaceholder = 1026;
    constexpr int kIdHardwarePlaceholder = 1027;
    constexpr int kIdQuickToolsTitle = 1028;
    constexpr int kIdQuickToolsHint = 1029;
    constexpr int kIdQuickToolPort = 41000;
    constexpr int kIdQuickToolPattern = 41001;
    constexpr int kIdQuickToolSmartDelete = 41002;

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

            if (id == kIdQuickToolPort)
            {
                MessageBoxW(hwnd_, L"Port-based killer module is scheduled for the next implementation phase.", L"Quick Kill Tools", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            if (id == kIdQuickToolPattern)
            {
                MessageBoxW(hwnd_, L"Pattern process killer module is scheduled for the next implementation phase.", L"Quick Kill Tools", MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            if (id == kIdQuickToolSmartDelete)
            {
                MessageBoxW(hwnd_, L"Smart Delete++ module is scheduled for the next implementation phase.", L"Quick Kill Tools", MB_OK | MB_ICONINFORMATION);
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

            if (control == quickToolsHint_ || control == performancePlaceholder_ || control == networkPlaceholder_ || control == hardwarePlaceholder_)
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
            if (GetParent(control) == sidebar_)
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
                sidebar_,
                MenuId(id),
                instance_,
                nullptr);
        };

        navProcesses_ = createNavButton(kIdNavProcesses, L"Processes", WS_GROUP);
        navPerformance_ = createNavButton(kIdNavPerformance, L"Performance", 0);
        navNetwork_ = createNavButton(kIdNavNetwork, L"Network", 0);
        navHardware_ = createNavButton(kIdNavHardware, L"Hardware", 0);
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
            L"Performance graphs and utilization panels are coming next.\r\n\r\nThe layout is ready for CPU, Memory, Disk, and GPU tiles.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdPerfPlaceholder),
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
            L"Use these tools for targeted process cleanup workflows.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolsHint),
            instance_,
            nullptr);

        quickKillPortButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Port-Based Killer",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolPort),
            instance_,
            nullptr);

        quickKillPatternButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Pattern Process Killer",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdQuickToolPattern),
            instance_,
            nullptr);

        quickKillSmartDeleteButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Smart Delete++",
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

        if (!sidebarTitle_ || !navProcesses_ || !navPerformance_ || !navNetwork_ || !navHardware_ || !navQuickTools_ ||
            !sectionTitle_ || !filterLabel_ || !filterEdit_ || !processList_ ||
            !performancePlaceholder_ || !networkPlaceholder_ || !hardwarePlaceholder_ ||
            !quickToolsTitle_ || !quickToolsHint_ || !quickKillPortButton_ || !quickKillPatternButton_ || !quickKillSmartDeleteButton_ ||
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
        applyFont(navQuickTools_, uiBoldFont_);
        applyFont(sectionTitle_, uiTitleFont_);
        applyFont(filterLabel_, uiBoldFont_);
        applyFont(filterEdit_, uiFont_);
        applyFont(processList_, uiFont_);
        applyFont(performancePlaceholder_, uiFont_);
        applyFont(networkPlaceholder_, uiFont_);
        applyFont(hardwarePlaceholder_, uiFont_);
        applyFont(quickToolsTitle_, uiTitleFont_);
        applyFont(quickToolsHint_, uiFont_);
        applyFont(quickKillPortButton_, uiBoldFont_);
        applyFont(quickKillPatternButton_, uiBoldFont_);
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

        const int quickTop = bodyTop + 8;
        const int buttonWidth = (std::min)(320, contentWidth);
        MoveWindow(quickToolsTitle_, contentX, quickTop, contentWidth, 34, TRUE);
        MoveWindow(quickToolsHint_, contentX, quickTop + 36, contentWidth, 26, TRUE);
        MoveWindow(quickKillPortButton_, contentX, quickTop + 74, buttonWidth, 34, TRUE);
        MoveWindow(quickKillPatternButton_, contentX, quickTop + 116, buttonWidth, 34, TRUE);
        MoveWindow(quickKillSmartDeleteButton_, contentX, quickTop + 158, buttonWidth, 34, TRUE);

        SetWindowTextW(sectionTitle_, SectionTitleText().c_str());
        ApplySectionVisibility();
    }

    void MainWindow::ApplySectionVisibility()
    {
        const bool processTab = activeSection_ == Section::Processes;
        const bool perfTab = activeSection_ == Section::Performance;
        const bool networkTab = activeSection_ == Section::Network;
        const bool hardwareTab = activeSection_ == Section::Hardware;
        const bool quickToolsTab = activeSection_ == Section::QuickKillTools;

        ShowWindow(filterLabel_, processTab ? SW_SHOW : SW_HIDE);
        ShowWindow(filterEdit_, processTab ? SW_SHOW : SW_HIDE);
        ShowWindow(processList_, processTab ? SW_SHOW : SW_HIDE);

        ShowWindow(performancePlaceholder_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(networkPlaceholder_, networkTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwarePlaceholder_, hardwareTab ? SW_SHOW : SW_HIDE);

        ShowWindow(quickToolsTitle_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickToolsHint_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickKillPortButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
        ShowWindow(quickKillPatternButton_, quickToolsTab ? SW_SHOW : SW_HIDE);
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
        case Section::QuickKillTools:
            checked = kIdNavQuickTools;
            break;
        }

        CheckRadioButton(sidebar_, kIdNavProcesses, kIdNavQuickTools, checked);
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
        case Section::QuickKillTools:
            return L"Quick Kill Tools";
        default:
            return L"Ultimate Task Manager";
        }
    }

    void MainWindow::HandleSnapshotUpdate()
    {
        snapshot_ = engine_.GetSnapshot();
        ApplyFilterAndSort();

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
