#include <winsock2.h>

#include "ui/MainWindow.h"

#include "system/ntapi/NtApi.h"
#include "tools/process/ProcessActions.h"
#include "util/logging/Logger.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dxgi1_6.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cmath>
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
    constexpr int kIdPerfNavPanel = 1039;
    constexpr int kIdPerfNavAll = 1040;
    constexpr int kIdPerfNavCpu = 1041;
    constexpr int kIdPerfNavMemory = 1042;
    constexpr int kIdPerfNavDisk = 1043;
    constexpr int kIdPerfNavWifi = 1044;
    constexpr int kIdPerfNavEthernet = 1045;
    constexpr int kIdPerfNavGpu = 1046;
    constexpr int kIdPerfDetails = 1047;
    constexpr int kIdPerfNavDynamicBase = 1100;
    constexpr int kIdPerfNavDynamicMax = 1199;
    constexpr int kIdPerfCpuModeSingle = 1048;
    constexpr int kIdPerfCpuModePerCore = 1049;
    constexpr int kIdHardwareHint = 1050;
    constexpr int kIdHardwareList = 1051;
    constexpr int kIdHardwareStatus = 1052;
    constexpr int kIdHardwareRefreshButton = 1053;
    constexpr int kIdHardwareToggleButton = 1054;
    constexpr int kIdHardwareSearchLabel = 1055;
    constexpr int kIdHardwareSearchEdit = 1056;

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

    std::uint64_t MakeLuidKey(LUID luid)
    {
        const auto high = static_cast<std::uint32_t>(luid.HighPart);
        return (static_cast<std::uint64_t>(high) << 32) | static_cast<std::uint64_t>(luid.LowPart);
    }

    bool IsHexDigit(wchar_t ch)
    {
        return (ch >= L'0' && ch <= L'9') ||
               (ch >= L'a' && ch <= L'f') ||
               (ch >= L'A' && ch <= L'F');
    }

    bool TryParseHexUlong(const std::wstring &text, size_t start, size_t &end, unsigned long &value)
    {
        end = start;
        while (end < text.size() && IsHexDigit(text[end]))
        {
            ++end;
        }

        if (end <= start)
        {
            return false;
        }

        const std::wstring token = text.substr(start, end - start);
        value = std::wcstoul(token.c_str(), nullptr, 16);
        return true;
    }

    bool TryExtractLuidFromGpuInstanceName(const std::wstring &instanceName, LUID &luid)
    {
        std::wstring lowered = instanceName;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        const size_t luidTag = lowered.find(L"luid_0x");
        if (luidTag == std::wstring::npos)
        {
            return false;
        }

        size_t pos = luidTag + 7;
        unsigned long highPart = 0;
        size_t afterHigh = pos;
        if (!TryParseHexUlong(lowered, pos, afterHigh, highPart))
        {
            return false;
        }

        const size_t separator = lowered.find(L"_0x", afterHigh);
        if (separator == std::wstring::npos)
        {
            return false;
        }

        unsigned long lowPart = 0;
        size_t lowEnd = separator + 3;
        if (!TryParseHexUlong(lowered, separator + 3, lowEnd, lowPart))
        {
            return false;
        }

        luid.HighPart = static_cast<LONG>(highPart);
        luid.LowPart = lowPart;
        return true;
    }

    int NetworkTypeRank(ULONG ifType)
    {
        if (ifType == IF_TYPE_IEEE80211)
        {
            return 0;
        }
        if (ifType == IF_TYPE_ETHERNET_CSMACD)
        {
            return 1;
        }
        return 2;
    }

    std::wstring NetworkTypeLabel(ULONG ifType)
    {
        if (ifType == IF_TYPE_IEEE80211)
        {
            return L"Wi-Fi";
        }
        if (ifType == IF_TYPE_ETHERNET_CSMACD)
        {
            return L"Ethernet";
        }
        return L"Network";
    }

    std::wstring SockaddrToIpString(const SOCKADDR *sockaddr)
    {
        if (!sockaddr)
        {
            return L"N/A";
        }

        wchar_t buffer[64]{};
        DWORD length = static_cast<DWORD>(std::size(buffer));
        if (WSAAddressToStringW(
                const_cast<LPSOCKADDR>(sockaddr),
                sockaddr->sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6),
                nullptr,
                buffer,
                &length) == 0)
        {
            return buffer;
        }

        return L"N/A";
    }

    void UpdateDynamicScale(double &scale, const std::deque<double> &history, double floor, double growPadding)
    {
        double peak = 0.0;
        for (double value : history)
        {
            if (value > peak)
            {
                peak = value;
            }
        }

        double target = peak > 0.0 ? peak * growPadding : floor;
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

        if (PdhOpenQueryW(nullptr, 0, &diskQuery_) == ERROR_SUCCESS)
        {
            const bool readOk = PdhAddEnglishCounterW(diskQuery_, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &diskReadCounter_) == ERROR_SUCCESS;
            const bool writeOk = PdhAddEnglishCounterW(diskQuery_, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &diskWriteCounter_) == ERROR_SUCCESS;
            const bool activeOk = PdhAddEnglishCounterW(diskQuery_, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &diskActiveCounter_) == ERROR_SUCCESS;
            const bool latencyOk = PdhAddEnglishCounterW(diskQuery_, L"\\PhysicalDisk(_Total)\\Avg. Disk sec/Transfer", 0, &diskLatencyCounter_) == ERROR_SUCCESS;

            diskCounterReady_ = readOk && writeOk && activeOk && latencyOk && PdhCollectQueryData(diskQuery_) == ERROR_SUCCESS;
            if (!diskCounterReady_)
            {
                PdhCloseQuery(diskQuery_);
                diskQuery_ = nullptr;
                diskReadCounter_ = nullptr;
                diskWriteCounter_ = nullptr;
                diskActiveCounter_ = nullptr;
                diskLatencyCounter_ = nullptr;
            }
        }

        wchar_t sysDir[MAX_PATH]{};
        if (GetSystemDirectoryW(sysDir, static_cast<UINT>(std::size(sysDir))) > 2)
        {
            systemDrive_ = std::wstring(sysDir, sysDir + 2) + L"\\";
        }
        if (systemDrive_.empty())
        {
            systemDrive_ = L"C:\\";
        }

        switch (GetDriveTypeW(systemDrive_.c_str()))
        {
        case DRIVE_FIXED:
            diskType_ = L"Fixed";
            break;
        case DRIVE_REMOVABLE:
            diskType_ = L"Removable";
            break;
        case DRIVE_REMOTE:
            diskType_ = L"Network";
            break;
        case DRIVE_CDROM:
            diskType_ = L"Optical";
            break;
        default:
            diskType_ = L"Unknown";
            break;
        }

        UpdateNetworkAdapterInfo();

        sidebarNavigationComponent_.Attach(this);
        processListViewComponent_.Attach(this);
        performancePanelComponent_.Attach(this);
        hardwarePanelComponent_.Attach(this);
        quickToolsPanelComponent_.Attach(this);
        statusBarComponent_.Attach(this);
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

        if (diskQuery_)
        {
            PdhCloseQuery(diskQuery_);
            diskQuery_ = nullptr;
            diskReadCounter_ = nullptr;
            diskWriteCounter_ = nullptr;
            diskActiveCounter_ = nullptr;
            diskLatencyCounter_ = nullptr;
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

            LRESULT notifyResult = 0;
            if (processListViewComponent_.HandleNotify(hdr, lParam, notifyResult))
            {
                return notifyResult;
            }

            if (hardwarePanelComponent_.HandleNotify(hdr, lParam, notifyResult))
            {
                return notifyResult;
            }

            break;
        }

        case WM_COMMAND:
        {
            const UINT id = LOWORD(wParam);
            const UINT code = HIWORD(wParam);

            if (sidebarNavigationComponent_.HandleCommand(id) ||
                performancePanelComponent_.HandleCommand(id) ||
                processListViewComponent_.HandleCommand(id, code) ||
                hardwarePanelComponent_.HandleCommand(id, code) ||
                quickToolsPanelComponent_.HandleCommand(id))
            {
                return 0;
            }

            if (id >= kCommandEndTask && id <= kCommandAffinityCore1)
            {
                ExecuteProcessCommand(id);
                return 0;
            }

            break;
        }

        case WM_CTLCOLORSTATIC:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            SetBkMode(dc, TRANSPARENT);

            if (sidebarNavigationComponent_.IsSidebarStaticControl(control))
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

            if (control == hardwarePlaceholder_)
            {
                SetBkColor(dc, kMainBackgroundColor);
                SetTextColor(dc, RGB(40, 57, 82));
                return reinterpret_cast<LRESULT>(backgroundBrush_);
            }

            if (control == perfNavPanel_ || control == perfDetails_)
            {
                SetBkColor(dc, kCardColor);
                SetTextColor(dc, RGB(35, 49, 72));
                return reinterpret_cast<LRESULT>(cardBrush_);
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
                control == hardwareHint_ ||
                control == hardwareSearchLabel_ ||
                control == hardwareStatus_ ||
                control == servicesPlaceholder_ ||
                control == startupAppsPlaceholder_ ||
                control == usersPlaceholder_)
            {
                SetBkColor(dc, kMainBackgroundColor);
                SetTextColor(dc, RGB(76, 92, 118));
                return reinterpret_cast<LRESULT>(backgroundBrush_);
            }

            if (statusBarComponent_.IsStatusControl(control))
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
            if (sidebarNavigationComponent_.IsSidebarButtonControl(control))
            {
                SetBkColor(dc, kSidebarBackgroundColor);
                SetTextColor(dc, RGB(32, 48, 76));
                return reinterpret_cast<LRESULT>(sidebarBrush_);
            }

            if (performancePanelComponent_.IsPerformanceButtonControl(control))
            {
                SetBkColor(dc, kCardColor);
                SetTextColor(dc, RGB(32, 48, 76));
                return reinterpret_cast<LRESULT>(cardBrush_);
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

        case WM_MOUSEWHEEL:
        {
            if (performancePanelComponent_.HandleMouseWheel(wParam, lParam))
            {
                return 0;
            }

            break;
        }

        case WM_DRAWITEM:
        {
            const auto *draw = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
            if (!draw)
            {
                break;
            }

            LRESULT drawResult = 0;
            if (performancePanelComponent_.HandleDrawItem(draw, drawResult))
            {
                return drawResult;
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

    bool MainWindow::HandleQuickToolsCommand(UINT id)
    {
        if (id == kIdQuickToolPortKillAll || id == kIdQuickToolPortKillOneByOne)
        {
            wchar_t input[512]{};
            GetWindowTextW(quickPortEdit_, input, static_cast<int>(std::size(input)));
            const auto ports = ParsePorts(input);
            if (ports.empty())
            {
                MessageBoxW(hwnd_, L"Enter one or more ports (examples: 5000 3000 3690 6699).", L"Port Killer", MB_OK | MB_ICONINFORMATION);
                return true;
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
                return true;
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
            return true;
        }

        if (id == kIdQuickToolProcessKillAll || id == kIdQuickToolProcessKillOneByOne)
        {
            wchar_t input[512]{};
            GetWindowTextW(quickProcessEdit_, input, static_cast<int>(std::size(input)));
            const auto tokens = SplitTokens(input);
            if (tokens.empty())
            {
                MessageBoxW(hwnd_, L"Enter one or more process patterns (examples: node ollama chrome).", L"Process Killer", MB_OK | MB_ICONINFORMATION);
                return true;
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
                return true;
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
            return true;
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
            return true;
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
            return true;
        }

        if (id == kIdQuickToolSmartDelete)
        {
            if (quickDeleteTargetPath_.empty())
            {
                MessageBoxW(hwnd_, L"Select a file or folder first.", L"Force Delete", MB_OK | MB_ICONINFORMATION);
                return true;
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
            return true;
        }

        return false;
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

        perfNavPanel_ = CreateWindowExW(
            0,
            L"STATIC",
            nullptr,
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdPerfNavPanel),
            instance_,
            nullptr);

        auto createPerfNavButton = [&](int id, const wchar_t *label, DWORD extraStyle)
        {
            return CreateWindowExW(
                0,
                L"BUTTON",
                label,
                WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE | extraStyle,
                0,
                0,
                0,
                0,
                hwnd_,
                MenuId(id),
                instance_,
                nullptr);
        };

        perfNavAll_ = createPerfNavButton(kIdPerfNavAll, L"All Stats", WS_GROUP);
        perfNavCpu_ = createPerfNavButton(kIdPerfNavCpu, L"CPU", 0);
        perfNavMemory_ = createPerfNavButton(kIdPerfNavMemory, L"Memory", 0);
        perfNavDisk_ = createPerfNavButton(kIdPerfNavDisk, L"Disk", 0);
        perfNavWifi_ = createPerfNavButton(kIdPerfNavWifi, L"Wi-Fi", 0);
        perfNavEthernet_ = createPerfNavButton(kIdPerfNavEthernet, L"Ethernet", 0);
        perfNavGpu_ = createPerfNavButton(kIdPerfNavGpu, L"GPU", 0);
        perfCpuModeSingle_ = createPerfNavButton(kIdPerfCpuModeSingle, L"Single Graph", WS_GROUP);
        perfCpuModePerCore_ = createPerfNavButton(kIdPerfCpuModePerCore, L"Per-Core Graphs", 0);

        perfDetails_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"STATIC",
            L"Performance details",
            WS_CHILD | SS_LEFT,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdPerfDetails),
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
            L"Hardware Devices",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwarePlaceholder),
            instance_,
            nullptr);

        hardwareHint_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Dynamic inventory of detected hardware components and ports. Select a device, then disable or enable it.",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwareHint),
            instance_,
            nullptr);

        hardwareSearchLabel_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Search",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwareSearchLabel),
            instance_,
            nullptr);

        hardwareSearchEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            nullptr,
            WS_CHILD | ES_AUTOHSCROLL,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwareSearchEdit),
            instance_,
            nullptr);

        hardwareRefreshButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Refresh Devices",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwareRefreshButton),
            instance_,
            nullptr);

        hardwareToggleButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Disable Selected",
            WS_CHILD | BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwareToggleButton),
            instance_,
            nullptr);

        hardwareList_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWW,
            nullptr,
            WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwareList),
            instance_,
            nullptr);

        hardwareStatus_ = CreateWindowExW(
            0,
            L"STATIC",
            L"Scanning hardware devices...",
            WS_CHILD,
            0,
            0,
            0,
            0,
            hwnd_,
            MenuId(kIdHardwareStatus),
            instance_,
            nullptr);

        if (hardwareList_)
        {
            SetWindowTheme(hardwareList_, L"Explorer", nullptr);
            ListView_SetExtendedListViewStyle(
                hardwareList_,
                LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);

            struct HardwareColumn
            {
                const wchar_t *title;
                int width;
            };

            const HardwareColumn columns[] = {
                {L"Device", 340},
                {L"Class", 170},
                {L"State", 120},
                {L"Location", 290}};

            for (int i = 0; i < static_cast<int>(std::size(columns)); ++i)
            {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.pszText = const_cast<LPWSTR>(columns[i].title);
                column.cx = columns[i].width;
                column.iSubItem = i;
                ListView_InsertColumn(hardwareList_, i, &column);
            }
        }

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
            !performancePlaceholder_ || !perfNavPanel_ || !perfNavAll_ || !perfNavCpu_ || !perfNavMemory_ || !perfNavDisk_ || !perfNavWifi_ || !perfNavEthernet_ || !perfNavGpu_ ||
            !perfCpuModeSingle_ || !perfCpuModePerCore_ ||
            !perfGraphCpu_ || !perfGraphMemory_ || !perfGraphGpu_ || !perfGraphUpload_ || !perfGraphDownload_ || !perfCoreGrid_ || !perfDetails_ ||
            !networkPlaceholder_ || !hardwarePlaceholder_ || !hardwareHint_ || !hardwareSearchLabel_ || !hardwareSearchEdit_ || !hardwareList_ || !hardwareRefreshButton_ || !hardwareToggleButton_ || !hardwareStatus_ ||
            !servicesPlaceholder_ || !startupAppsPlaceholder_ || !usersPlaceholder_ ||
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
        applyFont(perfNavPanel_, uiFont_);
        applyFont(perfNavAll_, uiBoldFont_);
        applyFont(perfNavCpu_, uiBoldFont_);
        applyFont(perfNavMemory_, uiBoldFont_);
        applyFont(perfNavDisk_, uiBoldFont_);
        applyFont(perfNavWifi_, uiBoldFont_);
        applyFont(perfNavEthernet_, uiBoldFont_);
        applyFont(perfNavGpu_, uiBoldFont_);
        applyFont(perfCpuModeSingle_, uiBoldFont_);
        applyFont(perfCpuModePerCore_, uiBoldFont_);
        applyFont(perfCoreGrid_, uiFont_);
        applyFont(perfDetails_, uiFont_);
        applyFont(networkPlaceholder_, uiFont_);
        applyFont(hardwarePlaceholder_, uiFont_);
        applyFont(hardwareHint_, uiFont_);
        applyFont(hardwareSearchLabel_, uiBoldFont_);
        applyFont(hardwareSearchEdit_, uiFont_);
        applyFont(hardwareList_, uiFont_);
        applyFont(hardwareRefreshButton_, uiBoldFont_);
        applyFont(hardwareToggleButton_, uiBoldFont_);
        applyFont(hardwareStatus_, uiFont_);
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

        RebuildPerformanceSubviewNavigation();
        sidebarNavigationComponent_.UpdateSelection();
        UpdatePerformanceSubviewSelection();
        RefreshHardwareInventory(false);
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
        const int hardwareButtonWidth = (std::max)(120, (std::min)(180, (contentWidth - 12) / 3));
        const int hardwareHeaderWidth = (std::max)(120, contentWidth - (hardwareButtonWidth * 2) - 16);
        MoveWindow(hardwarePlaceholder_, contentX, bodyTop, hardwareHeaderWidth, 24, TRUE);
        MoveWindow(hardwareHint_, contentX, bodyTop + 24, contentWidth, 24, TRUE);
        MoveWindow(hardwareRefreshButton_, contentX + contentWidth - (hardwareButtonWidth * 2) - 8, bodyTop, hardwareButtonWidth, 30, TRUE);
        MoveWindow(hardwareToggleButton_, contentX + contentWidth - hardwareButtonWidth, bodyTop, hardwareButtonWidth, 30, TRUE);

        const int hardwareSearchTop = bodyTop + 50;
        MoveWindow(hardwareSearchLabel_, contentX, hardwareSearchTop + 3, 56, 22, TRUE);
        MoveWindow(hardwareSearchEdit_, contentX + 58, hardwareSearchTop, (std::max)(120, contentWidth - 58), 28, TRUE);

        const int hardwareListTop = hardwareSearchTop + 34;
        const int hardwareStatusHeight = 24;
        const int hardwareListHeight = (std::max)(80, bodyHeight - (hardwareListTop - bodyTop) - hardwareStatusHeight - 6);
        MoveWindow(hardwareList_, contentX, hardwareListTop, contentWidth, hardwareListHeight, TRUE);
        MoveWindow(hardwareStatus_, contentX, hardwareListTop + hardwareListHeight + 4, contentWidth, hardwareStatusHeight, TRUE);
        MoveWindow(servicesPlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);
        MoveWindow(startupAppsPlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);
        MoveWindow(usersPlaceholder_, contentX, bodyTop, contentWidth, bodyHeight, TRUE);

        const int perfGap = 8;
        const int perfSidebarWidth = (std::min)(210, (std::max)(150, contentWidth / 4));
        const int perfMainX = contentX + perfSidebarWidth + 10;
        const int perfMainWidth = (std::max)(140, contentWidth - perfSidebarWidth - 10);
        const int perfHeaderHeight = 22;
        const bool cpuModeControlsVisible = activePerformanceView_ == PerformanceView::Cpu;
        const int cpuModeHeight = cpuModeControlsVisible ? 28 : 0;
        const int cpuModeGap = cpuModeControlsVisible ? 8 : 0;
        const int cpuModeTop = bodyTop + perfHeaderHeight + 4;
        const int perfDetailsHeight = (std::max)(120, (std::min)(190, bodyHeight / 3));
        const int perfGraphTop = bodyTop + perfHeaderHeight + 6 + cpuModeHeight + cpuModeGap;
        const int perfGraphHeight = (std::max)(120, statusY - perfGraphTop - perfDetailsHeight - kPadding);
        const int perfDetailsTop = perfGraphTop + perfGraphHeight + kPadding;

        MoveWindow(perfNavPanel_, contentX, bodyTop, perfSidebarWidth, bodyHeight, TRUE);

        const int perfNavX = contentX + 8;
        const int perfNavW = perfSidebarWidth - 16;
        int perfNavY = bodyTop + 8;
        MoveWindow(perfNavAll_, perfNavX, perfNavY, perfNavW, 30, TRUE);
        perfNavY += 36;
        MoveWindow(perfNavCpu_, perfNavX, perfNavY, perfNavW, 30, TRUE);
        perfNavY += 36;
        MoveWindow(perfNavMemory_, perfNavX, perfNavY, perfNavW, 30, TRUE);
        perfNavY += 36;
        MoveWindow(perfNavDisk_, perfNavX, perfNavY, perfNavW, 30, TRUE);
        perfNavY += 36;

        if (perfStaticDynamicButtonCount_ >= 1)
        {
            MoveWindow(perfNavWifi_, perfNavX, perfNavY, perfNavW, 30, TRUE);
            perfNavY += 36;
        }

        if (perfStaticDynamicButtonCount_ >= 2)
        {
            MoveWindow(perfNavEthernet_, perfNavX, perfNavY, perfNavW, 30, TRUE);
            perfNavY += 36;
        }

        if (perfStaticDynamicButtonCount_ >= 3)
        {
            MoveWindow(perfNavGpu_, perfNavX, perfNavY, perfNavW, 30, TRUE);
            perfNavY += 36;
        }

        for (HWND extraButton : perfNavExtraButtons_)
        {
            MoveWindow(extraButton, perfNavX, perfNavY, perfNavW, 30, TRUE);
            perfNavY += 36;
        }

        MoveWindow(performancePlaceholder_, perfMainX, bodyTop, perfMainWidth, perfHeaderHeight, TRUE);
        if (cpuModeControlsVisible)
        {
            const int modeGap = 8;
            const int modeSingleWidth = (perfMainWidth - modeGap) / 2;
            MoveWindow(perfCpuModeSingle_, perfMainX, cpuModeTop, modeSingleWidth, cpuModeHeight, TRUE);
            MoveWindow(perfCpuModePerCore_, perfMainX + modeSingleWidth + modeGap, cpuModeTop, perfMainWidth - modeSingleWidth - modeGap, cpuModeHeight, TRUE);
        }
        MoveWindow(perfDetails_, perfMainX, perfDetailsTop, perfMainWidth, perfDetailsHeight, TRUE);

        MoveWindow(perfGraphCpu_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
        MoveWindow(perfGraphMemory_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
        MoveWindow(perfGraphGpu_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
        MoveWindow(perfGraphUpload_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
        MoveWindow(perfGraphDownload_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
        MoveWindow(perfCoreGrid_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);

        if (activePerformanceView_ == PerformanceView::Cpu)
        {
            if (cpuGraphMode_ == CpuGraphMode::Single)
            {
                MoveWindow(perfGraphCpu_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
            }
            else
            {
                MoveWindow(perfCoreGrid_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
            }
        }
        else if (activePerformanceView_ == PerformanceView::Memory)
        {
            const int memMainHeight = (std::max)(120, (perfGraphHeight * 2) / 3);
            const int memSubHeight = (std::max)(80, perfGraphHeight - memMainHeight - perfGap);
            MoveWindow(perfGraphMemory_, perfMainX, perfGraphTop, perfMainWidth, memMainHeight, TRUE);
            MoveWindow(perfGraphGpu_, perfMainX, perfGraphTop + memMainHeight + perfGap, perfMainWidth, memSubHeight, TRUE);
        }
        else if (activePerformanceView_ == PerformanceView::Disk ||
                 activePerformanceView_ == PerformanceView::Wifi ||
                 activePerformanceView_ == PerformanceView::Ethernet)
        {
            const int perfHalfWidth = (perfMainWidth - perfGap) / 2;
            MoveWindow(perfGraphUpload_, perfMainX, perfGraphTop, perfHalfWidth, perfGraphHeight, TRUE);
            MoveWindow(perfGraphDownload_, perfMainX + perfHalfWidth + perfGap, perfGraphTop, perfMainWidth - perfHalfWidth - perfGap, perfGraphHeight, TRUE);
        }
        else if (activePerformanceView_ == PerformanceView::Gpu ||
                 activePerformanceView_ == PerformanceView::All)
        {
            MoveWindow(perfCoreGrid_, perfMainX, perfGraphTop, perfMainWidth, perfGraphHeight, TRUE);
        }

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

        SetWindowTextW(sectionTitle_, sidebarNavigationComponent_.ActiveSectionTitle().c_str());
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
        ShowWindow(perfNavPanel_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfNavAll_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfNavCpu_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfNavMemory_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfNavDisk_, perfTab ? SW_SHOW : SW_HIDE);
        ShowWindow(perfNavWifi_, (perfTab && perfStaticDynamicButtonCount_ >= 1) ? SW_SHOW : SW_HIDE);
        ShowWindow(perfNavEthernet_, (perfTab && perfStaticDynamicButtonCount_ >= 2) ? SW_SHOW : SW_HIDE);
        ShowWindow(perfNavGpu_, (perfTab && perfStaticDynamicButtonCount_ >= 3) ? SW_SHOW : SW_HIDE);
        for (HWND extraButton : perfNavExtraButtons_)
        {
            ShowWindow(extraButton, perfTab ? SW_SHOW : SW_HIDE);
        }
        ShowWindow(perfCpuModeSingle_, (perfTab && activePerformanceView_ == PerformanceView::Cpu) ? SW_SHOW : SW_HIDE);
        ShowWindow(perfCpuModePerCore_, (perfTab && activePerformanceView_ == PerformanceView::Cpu) ? SW_SHOW : SW_HIDE);
        ShowWindow(perfDetails_, perfTab ? SW_SHOW : SW_HIDE);

        const bool showAll = perfTab && activePerformanceView_ == PerformanceView::All;
        const bool showCpu = perfTab && activePerformanceView_ == PerformanceView::Cpu;
        const bool showMemory = perfTab && activePerformanceView_ == PerformanceView::Memory;
        const bool showDisk = perfTab && activePerformanceView_ == PerformanceView::Disk;
        const bool showWifi = perfTab && activePerformanceView_ == PerformanceView::Wifi;
        const bool showEthernet = perfTab && activePerformanceView_ == PerformanceView::Ethernet;
        const bool showGpu = perfTab && activePerformanceView_ == PerformanceView::Gpu;

        const bool showCpuSingleGraph = showCpu && cpuGraphMode_ == CpuGraphMode::Single;
        const bool showCpuPerCoreGrid = showCpu && cpuGraphMode_ == CpuGraphMode::PerCore;

        ShowWindow(perfGraphCpu_, showCpuSingleGraph ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphMemory_, showMemory ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphGpu_, showMemory ? SW_SHOW : SW_HIDE);
        ShowWindow(perfCoreGrid_, (showCpuPerCoreGrid || showGpu || showAll) ? SW_SHOW : SW_HIDE);

        const bool showDualThroughput = showDisk || showWifi || showEthernet;
        ShowWindow(perfGraphUpload_, showDualThroughput ? SW_SHOW : SW_HIDE);
        ShowWindow(perfGraphDownload_, showDualThroughput ? SW_SHOW : SW_HIDE);

        ShowWindow(networkPlaceholder_, networkTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwarePlaceholder_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwareHint_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwareSearchLabel_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwareSearchEdit_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwareList_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwareRefreshButton_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwareToggleButton_, hardwareTab ? SW_SHOW : SW_HIDE);
        ShowWindow(hardwareStatus_, hardwareTab ? SW_SHOW : SW_HIDE);
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

        if (hardwareTab)
        {
            RefreshHardwareActionState();
        }
    }

    void MainWindow::SetActiveSection(Section section)
    {
        if (activeSection_ == section)
        {
            return;
        }

        activeSection_ = section;
        sidebarNavigationComponent_.UpdateSelection();
        UpdatePerformanceSubviewSelection();
        LayoutControls();

        if (activeSection_ == Section::Processes)
        {
            RefreshProcessView();
        }
        else if (activeSection_ == Section::Performance)
        {
            RebuildPerformanceSubviewNavigation();
            RefreshPerformancePanel();
        }
        else if (activeSection_ == Section::Hardware)
        {
            RefreshHardwareInventory(true);
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

    double MainWindow::CalculateMemoryAxisTopGb(double totalMemoryGb)
    {
        if (totalMemoryGb <= 0.0)
        {
            return 1.0;
        }

        const double rounded = std::round(totalMemoryGb);
        return (std::max)(1.0, rounded);
    }

    double MainWindow::QueryGpuUsagePercent()
    {
        double highest = 0.0;
        for (const auto &gpu : gpuDevices_)
        {
            highest = (std::max)(highest, gpu.utilizationPercent);
        }

        return highest;
    }

    void MainWindow::UpdateNetworkAdapterInfo()
    {
        wifiAdapterName_ = L"N/A";
        wifiIpv4_ = L"N/A";
        wifiIpv6_ = L"N/A";
        ethernetAdapterName_ = L"N/A";
        ethernetIpv4_ = L"N/A";
        ethernetIpv6_ = L"N/A";

        ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        ULONG size = 16 * 1024;
        std::vector<std::uint8_t> buffer(size);
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());

        ULONG result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
        if (result == ERROR_BUFFER_OVERFLOW)
        {
            buffer.resize(size);
            adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
            result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
        }

        if (result != NO_ERROR)
        {
            return;
        }

        std::unordered_map<std::wstring, NetworkInterfacePerf> previousByKey;
        previousByKey.reserve(networkInterfaces_.size());
        for (auto &iface : networkInterfaces_)
        {
            previousByKey.emplace(iface.key, std::move(iface));
        }

        std::vector<NetworkInterfacePerf> refreshed;

        for (const auto *adapter = adapters; adapter; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp)
            {
                continue;
            }

            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || adapter->IfType == IF_TYPE_TUNNEL)
            {
                continue;
            }

            const std::wstring key = std::to_wstring(adapter->Luid.Value);
            NetworkInterfacePerf iface{};
            if (const auto it = previousByKey.find(key); it != previousByKey.end())
            {
                iface = std::move(it->second);
            }

            iface.key = key;
            iface.ifType = adapter->IfType;
            iface.ifIndex = adapter->IfIndex;
            iface.adapterName = (adapter->FriendlyName && adapter->FriendlyName[0] != L'\0')
                                    ? adapter->FriendlyName
                                    : (L"Interface " + std::to_wstring(adapter->IfIndex));
            iface.ipv4 = L"N/A";
            iface.ipv6 = L"N/A";

            for (const auto *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
            {
                if (!unicast->Address.lpSockaddr)
                {
                    continue;
                }

                if (unicast->Address.lpSockaddr->sa_family == AF_INET && iface.ipv4 == L"N/A")
                {
                    iface.ipv4 = SockaddrToIpString(unicast->Address.lpSockaddr);
                }

                if (unicast->Address.lpSockaddr->sa_family == AF_INET6 && iface.ipv6 == L"N/A")
                {
                    iface.ipv6 = SockaddrToIpString(unicast->Address.lpSockaddr);
                }
            }

            refreshed.push_back(std::move(iface));
        }

        std::sort(refreshed.begin(), refreshed.end(), [](const NetworkInterfacePerf &a, const NetworkInterfacePerf &b)
                  {
                      const int rankA = NetworkTypeRank(a.ifType);
                      const int rankB = NetworkTypeRank(b.ifType);
                      if (rankA != rankB)
                      {
                          return rankA < rankB;
                      }
                      return a.adapterName < b.adapterName; });

        networkInterfaces_ = std::move(refreshed);

        for (const auto &iface : networkInterfaces_)
        {
            if (iface.ifType == IF_TYPE_IEEE80211 && wifiAdapterName_ == L"N/A")
            {
                wifiAdapterName_ = iface.adapterName;
                wifiIpv4_ = iface.ipv4;
                wifiIpv6_ = iface.ipv6;
            }

            if (iface.ifType == IF_TYPE_ETHERNET_CSMACD && ethernetAdapterName_ == L"N/A")
            {
                ethernetAdapterName_ = iface.adapterName;
                ethernetIpv4_ = iface.ipv4;
                ethernetIpv6_ = iface.ipv6;
            }
        }
    }

    std::wstring MainWindow::BuildPerformanceDetailsText() const
    {
        std::wstringstream text;

        switch (activePerformanceView_)
        {
        case PerformanceView::All:
            text << L"Overview mode: all detected devices are rendered in one scrollable canvas.\r\n"
                 << L"CPU " << static_cast<int>(totalCpuPercent_ + 0.5) << L"% | Memory " << FormatNumber(memoryUsedGb_, 1) << L" / " << FormatNumber(memoryTotalGb_, 1) << L" GB\r\n"
                 << L"Disk R/W " << FormatNumber(diskReadMBps_, 1) << L" / " << FormatNumber(diskWriteMBps_, 1) << L" MB/s | Active " << static_cast<int>(diskActivePercent_ + 0.5) << L"%\r\n"
                 << L"Network interfaces: " << networkInterfaces_.size()
                 << L" | Total U/D " << FormatNumber(uploadMbps_, 1) << L" / " << FormatNumber(downloadMbps_, 1) << L" Mbps\r\n"
                 << L"Detected GPUs: " << gpuDevices_.size()
                 << L" | Peak GPU " << static_cast<int>(gpuPercent_ + 0.5) << L"%";
            break;

        case PerformanceView::Cpu:
        {
            std::uint64_t totalThreads = 0;
            for (const auto &p : snapshot_.processes)
            {
                totalThreads += p.threadCount;
            }

            DWORD sockets = 0;
            DWORD cores = 0;
            DWORD logical = 0;
            std::uint64_t l1Kb = 0;
            std::uint64_t l2Kb = 0;
            std::uint64_t l3Kb = 0;

            DWORD infoSize = 0;
            GetLogicalProcessorInformationEx(RelationAll, nullptr, &infoSize);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && infoSize > 0)
            {
                std::vector<std::uint8_t> infoBuffer(infoSize);
                auto *info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(infoBuffer.data());
                if (GetLogicalProcessorInformationEx(RelationAll, info, &infoSize))
                {
                    auto countBits = [](KAFFINITY mask)
                    {
                        DWORD count = 0;
                        while (mask)
                        {
                            mask &= (mask - 1);
                            ++count;
                        }
                        return count;
                    };

                    BYTE *ptr = infoBuffer.data();
                    BYTE *end = infoBuffer.data() + infoSize;
                    while (ptr < end)
                    {
                        auto *entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
                        if (entry->Relationship == RelationProcessorPackage)
                        {
                            ++sockets;
                        }
                        else if (entry->Relationship == RelationProcessorCore)
                        {
                            ++cores;
                            for (WORD g = 0; g < entry->Processor.GroupCount; ++g)
                            {
                                logical += countBits(entry->Processor.GroupMask[g].Mask);
                            }
                        }
                        else if (entry->Relationship == RelationCache)
                        {
                            const std::uint64_t sizeKb = entry->Cache.CacheSize / 1024;
                            if (entry->Cache.Level == 1)
                            {
                                l1Kb = (std::max)(l1Kb, sizeKb);
                            }
                            else if (entry->Cache.Level == 2)
                            {
                                l2Kb = (std::max)(l2Kb, sizeKb);
                            }
                            else if (entry->Cache.Level == 3)
                            {
                                l3Kb = (std::max)(l3Kb, sizeKb);
                            }
                        }

                        ptr += entry->Size;
                    }
                }
            }

            if (logical == 0)
            {
                logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            }
            if (cores == 0)
            {
                cores = logical;
            }
            if (sockets == 0)
            {
                sockets = 1;
            }

            DWORD baseMhz = 0;
            HKEY cpuKey = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &cpuKey) == ERROR_SUCCESS)
            {
                DWORD type = 0;
                DWORD size = sizeof(baseMhz);
                if (RegQueryValueExW(cpuKey, L"~MHz", nullptr, &type, reinterpret_cast<LPBYTE>(&baseMhz), &size) != ERROR_SUCCESS)
                {
                    baseMhz = 0;
                }
                RegCloseKey(cpuKey);
            }

            const bool virtualization = IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) != FALSE;

            const std::uint64_t uptimeSeconds = GetTickCount64() / 1000;
            const std::uint64_t upDays = uptimeSeconds / 86400;
            const std::uint64_t upHours = (uptimeSeconds % 86400) / 3600;
            const std::uint64_t upMinutes = (uptimeSeconds % 3600) / 60;

            text << L"Utilization: " << static_cast<int>(totalCpuPercent_ + 0.5) << L"%\r\n"
                 << L"Graph mode: " << (cpuGraphMode_ == CpuGraphMode::Single ? L"Single graph" : L"Per-core graphs (scroll to view all)") << L"\r\n"
                 << L"Processes: " << snapshot_.processes.size() << L" | Threads: " << totalThreads << L"\r\n"
                 << L"Uptime: " << upDays << L"d " << upHours << L"h " << upMinutes << L"m\r\n"
                 << L"Base speed: " << (baseMhz > 0 ? std::to_wstring(baseMhz) + L" MHz" : L"N/A") << L"\r\n"
                 << L"Sockets: " << sockets << L" | Cores: " << cores << L" | Logical processors: " << logical << L"\r\n"
                 << L"Virtualization: " << (virtualization ? L"Enabled" : L"Disabled/Unknown") << L"\r\n"
                 << L"L1 cache: " << (l1Kb > 0 ? std::to_wstring(l1Kb) + L" KB" : L"N/A")
                 << L" | L2 cache: " << (l2Kb > 0 ? std::to_wstring(l2Kb) + L" KB" : L"N/A")
                 << L" | L3 cache: " << (l3Kb > 0 ? std::to_wstring(l3Kb) + L" KB" : L"N/A");
            break;
        }

        case PerformanceView::Memory:
        {
            text << L"In use: " << FormatNumber(memoryUsedGb_, 1) << L" GB\r\n"
                 << L"Available: " << FormatNumber(memoryAvailableGb_, 1) << L" GB\r\n"
                 << L"Committed: " << FormatNumber(memoryCommittedGb_, 1) << L" / " << FormatNumber(memoryCommitLimitGb_, 1) << L" GB\r\n"
                 << L"Utilization: " << static_cast<int>(memoryPercent_ + 0.5) << L"%\r\n"
                 << L"Design note: charts show usage and available headroom percentages.\r\n"
                 << L"Speed: N/A | Slots used: N/A | Form factor: N/A | Hardware reserved: N/A";
            break;
        }

        case PerformanceView::Disk:
        {
            ULARGE_INTEGER freeBytesAvailable{};
            ULARGE_INTEGER totalBytes{};
            ULARGE_INTEGER totalFree{};
            GetDiskFreeSpaceExW(systemDrive_.c_str(), &freeBytesAvailable, &totalBytes, &totalFree);

            PERFORMANCE_INFORMATION perf{};
            perf.cb = sizeof(perf);
            GetPerformanceInfo(&perf, sizeof(perf));
            const bool pageFileEnabled = perf.CommitLimit > perf.PhysicalTotal;

            text << L"Active time: " << static_cast<int>(diskActivePercent_ + 0.5) << L"%\r\n"
                 << L"Avg response time: " << FormatNumber(diskAvgResponseMs_, 2) << L" ms\r\n"
                 << L"Read speed: " << FormatNumber(diskReadMBps_, diskReadMBps_ < 10.0 ? 2 : 1) << L" MB/s\r\n"
                 << L"Write speed: " << FormatNumber(diskWriteMBps_, diskWriteMBps_ < 10.0 ? 2 : 1) << L" MB/s\r\n"
                 << L"Capacity: " << FormatNumber(static_cast<double>(totalBytes.QuadPart) / kBytesPerGiB, 1) << L" GB\r\n"
                 << L"Formatted size: " << FormatNumber(static_cast<double>(totalFree.QuadPart) / kBytesPerGiB, 1) << L" GB free\r\n"
                 << L"System disk: " << systemDrive_ << L" | Page file: " << (pageFileEnabled ? L"Yes" : L"No") << L" | Type: " << diskType_;
            break;
        }

        case PerformanceView::Wifi:
        {
            const auto *iface = GetActiveNetworkInterface();
            if (!iface)
            {
                text << L"No active Wi-Fi interfaces were detected.";
            }
            else
            {
                const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
                size_t wifiOrdinal = 0;
                for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
                {
                    if (networkInterfaces_[i].ifType == IF_TYPE_IEEE80211)
                    {
                        ++wifiOrdinal;
                    }
                }

                text << L"Wi-Fi " << (wifiOrdinal > 0 ? wifiOrdinal - 1 : 0) << L": " << iface->adapterName << L"\r\n"
                     << L"Upload: " << FormatNumber(iface->uploadMbps, iface->uploadMbps < 10.0 ? 2 : 1) << L" Mbps\r\n"
                     << L"Download: " << FormatNumber(iface->downloadMbps, iface->downloadMbps < 10.0 ? 2 : 1) << L" Mbps\r\n"
                     << L"IPv4: " << iface->ipv4 << L"\r\n"
                     << L"IPv6: " << iface->ipv6;
            }
            break;
        }

        case PerformanceView::Ethernet:
        {
            const auto *iface = GetActiveNetworkInterface();
            if (!iface)
            {
                text << L"No active Ethernet interfaces were detected.";
            }
            else
            {
                const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
                size_t ordinal = 0;
                for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
                {
                    if (networkInterfaces_[i].ifType != IF_TYPE_IEEE80211)
                    {
                        ++ordinal;
                    }
                }

                text << NetworkTypeLabel(iface->ifType) << L" " << (ordinal > 0 ? ordinal - 1 : 0) << L": " << iface->adapterName << L"\r\n"
                     << L"Upload: " << FormatNumber(iface->uploadMbps, iface->uploadMbps < 10.0 ? 2 : 1) << L" Mbps\r\n"
                     << L"Download: " << FormatNumber(iface->downloadMbps, iface->downloadMbps < 10.0 ? 2 : 1) << L" Mbps\r\n"
                     << L"IPv4: " << iface->ipv4 << L"\r\n"
                     << L"IPv6: " << iface->ipv6;
            }
            break;
        }

        case PerformanceView::Gpu:
        {
            const auto *gpu = GetActiveGpu();
            if (!gpu)
            {
                text << L"No hardware GPU adapters were detected.";
                break;
            }

            const size_t gpuIndex = static_cast<size_t>(gpu - gpuDevices_.data());
            text << L"GPU " << (gpuIndex + 1) << L": " << gpu->name << L"\r\n"
                 << L"Utilization: " << static_cast<int>(gpu->utilizationPercent + 0.5) << L"%\r\n"
                 << L"3D " << static_cast<int>(gpu->engine3dPercent + 0.5)
                 << L"% | Copy " << static_cast<int>(gpu->engineCopyPercent + 0.5)
                 << L"% | Decode " << static_cast<int>(gpu->engineDecodePercent + 0.5)
                 << L"% | Encode " << static_cast<int>(gpu->engineEncodePercent + 0.5) << L"%\r\n"
                 << L"Dedicated memory: " << FormatNumber(gpu->dedicatedUsedGb, 1) << L" / " << FormatNumber(gpu->dedicatedGb, 1) << L" GB\r\n"
                 << L"Shared memory: " << FormatNumber(gpu->sharedUsedGb, 1) << L" / " << FormatNumber(gpu->sharedGb, 1) << L" GB\r\n"
                 << L"Location: " << gpu->location;
            break;
        }
        }

        return text.str();
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
            memoryAvailableGb_ = (std::max)(0.0, memoryTotalGb_ - memoryUsedGb_);
            memoryAxisTopGb_ = CalculateMemoryAxisTopGb(memoryTotalGb_);
        }

        PERFORMANCE_INFORMATION perfInfo{};
        perfInfo.cb = sizeof(perfInfo);
        if (GetPerformanceInfo(&perfInfo, sizeof(perfInfo)))
        {
            const double pageSize = static_cast<double>(perfInfo.PageSize);
            memoryCommittedGb_ = (perfInfo.CommitTotal * pageSize) / kBytesPerGiB;
            memoryCommitLimitGb_ = (perfInfo.CommitLimit * pageSize) / kBytesPerGiB;
        }

        auto readCounterValue = [](PDH_HCOUNTER counter)
        {
            if (!counter)
            {
                return 0.0;
            }

            PDH_FMT_COUNTERVALUE value{};
            if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS)
            {
                return 0.0;
            }

            if (value.CStatus != ERROR_SUCCESS)
            {
                return 0.0;
            }

            return value.doubleValue;
        };

        if (diskCounterReady_ && diskQuery_)
        {
            if (PdhCollectQueryData(diskQuery_) == ERROR_SUCCESS)
            {
                const double readBytesPerSec = readCounterValue(diskReadCounter_);
                const double writeBytesPerSec = readCounterValue(diskWriteCounter_);
                const double activePercent = readCounterValue(diskActiveCounter_);
                const double avgSec = readCounterValue(diskLatencyCounter_);

                diskReadMBps_ = (std::max)(0.0, readBytesPerSec / (1024.0 * 1024.0));
                diskWriteMBps_ = (std::max)(0.0, writeBytesPerSec / (1024.0 * 1024.0));
                diskActivePercent_ = (std::clamp)(activePercent, 0.0, 100.0);
                diskAvgResponseMs_ = (std::max)(0.0, avgSec * 1000.0);
            }
        }

        UpdateNetworkAdapterInfo();

        const std::uint64_t nowMs = GetTickCount64();
        const double elapsedSec = (networkSamplingReady_ && nowMs > previousNetworkTickMs_)
                                      ? static_cast<double>(nowMs - previousNetworkTickMs_) / 1000.0
                                      : 0.0;

        uploadMbps_ = 0.0;
        downloadMbps_ = 0.0;
        wifiUploadMbps_ = 0.0;
        wifiDownloadMbps_ = 0.0;
        ethernetUploadMbps_ = 0.0;
        ethernetDownloadMbps_ = 0.0;

        for (auto &iface : networkInterfaces_)
        {
            MIB_IFROW row{};
            row.dwIndex = iface.ifIndex;
            if (GetIfEntry(&row) != NO_ERROR)
            {
                iface.uploadMbps = 0.0;
                iface.downloadMbps = 0.0;
                PushHistory(iface.uploadHistory, iface.uploadMbps);
                PushHistory(iface.downloadHistory, iface.downloadMbps);
                continue;
            }

            const std::uint64_t inBytes = row.dwInOctets;
            const std::uint64_t outBytes = row.dwOutOctets;

            if (elapsedSec > 0.0001 && iface.hasPreviousSample)
            {
                const std::uint64_t inDelta = inBytes >= iface.previousInBytes ? inBytes - iface.previousInBytes : 0;
                const std::uint64_t outDelta = outBytes >= iface.previousOutBytes ? outBytes - iface.previousOutBytes : 0;

                iface.downloadMbps = (static_cast<double>(inDelta) * 8.0) / (elapsedSec * 1000.0 * 1000.0);
                iface.uploadMbps = (static_cast<double>(outDelta) * 8.0) / (elapsedSec * 1000.0 * 1000.0);
            }
            else
            {
                iface.downloadMbps = 0.0;
                iface.uploadMbps = 0.0;
            }

            iface.previousInBytes = inBytes;
            iface.previousOutBytes = outBytes;
            iface.hasPreviousSample = true;

            PushHistory(iface.uploadHistory, iface.uploadMbps);
            PushHistory(iface.downloadHistory, iface.downloadMbps);

            uploadMbps_ += iface.uploadMbps;
            downloadMbps_ += iface.downloadMbps;

            if (iface.ifType == IF_TYPE_IEEE80211)
            {
                wifiUploadMbps_ += iface.uploadMbps;
                wifiDownloadMbps_ += iface.downloadMbps;
            }
            else if (iface.ifType == IF_TYPE_ETHERNET_CSMACD)
            {
                ethernetUploadMbps_ += iface.uploadMbps;
                ethernetDownloadMbps_ += iface.downloadMbps;
            }
        }

        previousNetworkTickMs_ = nowMs;
        networkSamplingReady_ = true;

        std::unordered_map<std::uint64_t, GpuPerf> previousGpuByKey;
        previousGpuByKey.reserve(gpuDevices_.size());
        for (auto &gpu : gpuDevices_)
        {
            previousGpuByKey.emplace(MakeLuidKey(gpu.luid), std::move(gpu));
        }

        std::vector<GpuPerf> nextGpuDevices;

        IDXGIFactory1 *factory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))
        {
            UINT index = 0;
            IDXGIAdapter1 *adapter = nullptr;
            while (factory->EnumAdapters1(index, &adapter) == S_OK)
            {
                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
                {
                    const std::uint64_t key = MakeLuidKey(desc.AdapterLuid);

                    GpuPerf gpu{};
                    if (const auto it = previousGpuByKey.find(key); it != previousGpuByKey.end())
                    {
                        gpu = std::move(it->second);
                    }

                    gpu.luid = desc.AdapterLuid;
                    gpu.name = desc.Description[0] != L'\0' ? desc.Description : (L"GPU " + std::to_wstring(index + 1));

                    std::wstringstream location;
                    location << L"GPU " << (index + 1) << L" LUID " << desc.AdapterLuid.HighPart << L":" << desc.AdapterLuid.LowPart;
                    gpu.location = location.str();

                    gpu.dedicatedGb = static_cast<double>(desc.DedicatedVideoMemory) / kBytesPerGiB;
                    gpu.sharedGb = static_cast<double>(desc.SharedSystemMemory) / kBytesPerGiB;
                    gpu.dedicatedUsedGb = 0.0;
                    gpu.sharedUsedGb = 0.0;

                    gpu.utilizationPercent = 0.0;
                    gpu.engine3dPercent = 0.0;
                    gpu.engineCopyPercent = 0.0;
                    gpu.engineDecodePercent = 0.0;
                    gpu.engineEncodePercent = 0.0;

                    IDXGIAdapter3 *adapter3 = nullptr;
                    if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void **>(&adapter3))))
                    {
                        DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
                        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo)))
                        {
                            gpu.dedicatedUsedGb = static_cast<double>(localInfo.CurrentUsage) / kBytesPerGiB;
                        }

                        DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};
                        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo)))
                        {
                            gpu.sharedUsedGb = static_cast<double>(nonLocalInfo.CurrentUsage) / kBytesPerGiB;
                        }

                        adapter3->Release();
                    }

                    nextGpuDevices.push_back(std::move(gpu));
                }

                adapter->Release();
                adapter = nullptr;
                ++index;
            }

            factory->Release();
        }

        std::unordered_map<std::uint64_t, size_t> gpuIndexByLuid;
        gpuIndexByLuid.reserve(nextGpuDevices.size());
        for (size_t i = 0; i < nextGpuDevices.size(); ++i)
        {
            gpuIndexByLuid.emplace(MakeLuidKey(nextGpuDevices[i].luid), i);
        }

        std::vector<double> gpuTotalEngineUsage(nextGpuDevices.size(), 0.0);

        if (gpuCounterReady_ && gpuQuery_ && gpuCounter_ && !nextGpuDevices.empty())
        {
            if (PdhCollectQueryData(gpuQuery_) == ERROR_SUCCESS)
            {
                DWORD bufferSize = 0;
                DWORD itemCount = 0;
                PDH_STATUS counterStatus = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
                if ((counterStatus == ERROR_SUCCESS || counterStatus == PDH_MORE_DATA) && bufferSize > 0 && itemCount > 0)
                {
                    std::vector<std::uint8_t> buffer(bufferSize);
                    auto *items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
                    counterStatus = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
                    if (counterStatus == ERROR_SUCCESS)
                    {
                        for (DWORD i = 0; i < itemCount; ++i)
                        {
                            if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
                            {
                                continue;
                            }

                            const double value = (std::max)(0.0, items[i].FmtValue.doubleValue);
                            if (value <= 0.0)
                            {
                                continue;
                            }

                            LUID luid{};
                            if (!TryExtractLuidFromGpuInstanceName(items[i].szName ? items[i].szName : L"", luid))
                            {
                                continue;
                            }

                            const auto gpuIt = gpuIndexByLuid.find(MakeLuidKey(luid));
                            if (gpuIt == gpuIndexByLuid.end())
                            {
                                continue;
                            }

                            GpuPerf &gpu = nextGpuDevices[gpuIt->second];
                            gpuTotalEngineUsage[gpuIt->second] += value;

                            const std::wstring name = items[i].szName ? ToLower(items[i].szName) : L"";
                            if (name.find(L"engtype_3d") != std::wstring::npos)
                            {
                                gpu.engine3dPercent += value;
                            }
                            else if (name.find(L"engtype_copy") != std::wstring::npos)
                            {
                                gpu.engineCopyPercent += value;
                            }
                            else if (name.find(L"engtype_videodecode") != std::wstring::npos ||
                                     name.find(L"engtype_video decode") != std::wstring::npos)
                            {
                                gpu.engineDecodePercent += value;
                            }
                            else if (name.find(L"engtype_videoencode") != std::wstring::npos ||
                                     name.find(L"engtype_video encode") != std::wstring::npos)
                            {
                                gpu.engineEncodePercent += value;
                            }
                        }
                    }
                }
            }
        }

        for (size_t i = 0; i < nextGpuDevices.size(); ++i)
        {
            GpuPerf &gpu = nextGpuDevices[i];
            gpu.engine3dPercent = (std::clamp)(gpu.engine3dPercent, 0.0, 100.0);
            gpu.engineCopyPercent = (std::clamp)(gpu.engineCopyPercent, 0.0, 100.0);
            gpu.engineDecodePercent = (std::clamp)(gpu.engineDecodePercent, 0.0, 100.0);
            gpu.engineEncodePercent = (std::clamp)(gpu.engineEncodePercent, 0.0, 100.0);

            const double enginePeak = (std::max)((std::max)(gpu.engine3dPercent, gpu.engineCopyPercent),
                                                 (std::max)(gpu.engineDecodePercent, gpu.engineEncodePercent));
            const double fallback = (std::clamp)(gpuTotalEngineUsage[i], 0.0, 100.0);
            gpu.utilizationPercent = enginePeak > 0.0 ? enginePeak : fallback;

            PushHistory(gpu.utilizationHistory, gpu.utilizationPercent);
            PushHistory(gpu.dedicatedHistory, gpu.dedicatedUsedGb);
            PushHistory(gpu.sharedHistory, gpu.sharedUsedGb);
        }

        gpuDevices_ = std::move(nextGpuDevices);

        gpuAdapterName_ = L"N/A";
        gpuAdaptersSummary_.clear();
        gpuLocation_.clear();
        gpuDedicatedGb_ = 0.0;
        gpuSharedGb_ = 0.0;
        gpuDedicatedUsedGb_ = 0.0;
        gpuSharedUsedGb_ = 0.0;
        gpu3dPercent_ = 0.0;
        gpuCopyPercent_ = 0.0;
        gpuDecodePercent_ = 0.0;
        gpuEncodePercent_ = 0.0;

        for (size_t i = 0; i < gpuDevices_.size(); ++i)
        {
            const auto &gpu = gpuDevices_[i];

            if (gpuAdapterName_ == L"N/A")
            {
                gpuAdapterName_ = gpu.name;
            }

            if (!gpuAdaptersSummary_.empty())
            {
                gpuAdaptersSummary_ += L" | ";
            }
            gpuAdaptersSummary_ += gpu.name;

            if (!gpuLocation_.empty())
            {
                gpuLocation_ += L" | ";
            }
            gpuLocation_ += gpu.location;

            gpuDedicatedGb_ += gpu.dedicatedGb;
            gpuSharedGb_ += gpu.sharedGb;
            gpuDedicatedUsedGb_ += gpu.dedicatedUsedGb;
            gpuSharedUsedGb_ += gpu.sharedUsedGb;

            gpu3dPercent_ = (std::max)(gpu3dPercent_, gpu.engine3dPercent);
            gpuCopyPercent_ = (std::max)(gpuCopyPercent_, gpu.engineCopyPercent);
            gpuDecodePercent_ = (std::max)(gpuDecodePercent_, gpu.engineDecodePercent);
            gpuEncodePercent_ = (std::max)(gpuEncodePercent_, gpu.engineEncodePercent);
        }

        gpuPercent_ = QueryGpuUsagePercent();

        PushHistory(cpuHistory_, totalCpuPercent_);
        PushHistory(memoryHistory_, memoryUsedGb_);
        PushHistory(memoryUtilizationHistory_, memoryPercent_);
        PushHistory(memoryHeadroomHistory_, (std::clamp)(100.0 - memoryPercent_, 0.0, 100.0));
        PushHistory(memoryAvailableHistory_, memoryAvailableGb_);
        PushHistory(memoryCommittedHistory_, memoryCommittedGb_);
        PushHistory(gpuHistory_, gpuPercent_);
        PushHistory(gpu3dHistory_, gpu3dPercent_);
        PushHistory(gpuCopyHistory_, gpuCopyPercent_);
        PushHistory(gpuDecodeHistory_, gpuDecodePercent_);
        PushHistory(gpuEncodeHistory_, gpuEncodePercent_);
        PushHistory(gpuDedicatedHistory_, gpuDedicatedUsedGb_);
        PushHistory(gpuSharedHistory_, gpuSharedUsedGb_);
        PushHistory(uploadHistory_, uploadMbps_);
        PushHistory(downloadHistory_, downloadMbps_);
        PushHistory(wifiUploadHistory_, wifiUploadMbps_);
        PushHistory(wifiDownloadHistory_, wifiDownloadMbps_);
        PushHistory(ethernetUploadHistory_, ethernetUploadMbps_);
        PushHistory(ethernetDownloadHistory_, ethernetDownloadMbps_);
        PushHistory(diskReadHistory_, diskReadMBps_);
        PushHistory(diskWriteHistory_, diskWriteMBps_);
        PushHistory(diskActiveHistory_, diskActivePercent_);
    }

    void MainWindow::RefreshPerformancePanel()
    {
        if (!perfCoreGrid_ || !perfDetails_)
        {
            return;
        }

        RebuildPerformanceSubviewNavigation();
        const PerformanceView previousView = activePerformanceView_;
        NormalizeDynamicPerformanceSelection();
        if (previousView != activePerformanceView_ && activeSection_ == Section::Performance)
        {
            LayoutControls();
        }
        UpdatePerformanceSubviewSelection();

        UpdateDynamicScale(uploadScaleMbps_, uploadHistory_, 0.5, 1.25);
        UpdateDynamicScale(downloadScaleMbps_, downloadHistory_, 0.5, 1.25);
        UpdateDynamicScale(wifiUploadScaleMbps_, wifiUploadHistory_, 0.5, 1.25);
        UpdateDynamicScale(wifiDownloadScaleMbps_, wifiDownloadHistory_, 0.5, 1.25);
        UpdateDynamicScale(ethernetUploadScaleMbps_, ethernetUploadHistory_, 0.5, 1.25);
        UpdateDynamicScale(ethernetDownloadScaleMbps_, ethernetDownloadHistory_, 0.5, 1.25);
        UpdateDynamicScale(diskReadScaleMBps_, diskReadHistory_, 1.0, 1.25);
        UpdateDynamicScale(diskWriteScaleMBps_, diskWriteHistory_, 1.0, 1.25);

        for (auto &iface : networkInterfaces_)
        {
            UpdateDynamicScale(iface.uploadScaleMbps, iface.uploadHistory, 0.5, 1.25);
            UpdateDynamicScale(iface.downloadScaleMbps, iface.downloadHistory, 0.5, 1.25);
        }

        std::wstring title;
        switch (activePerformanceView_)
        {
        case PerformanceView::All:
            title = L"All Stats | Scroll to view every graph";
            break;
        case PerformanceView::Cpu:
            title = cpuGraphMode_ == CpuGraphMode::Single
                        ? L"CPU | Single graph mode"
                        : L"CPU | Per-core mode (scroll to view all cores)";
            break;
        case PerformanceView::Memory:
            title = L"Memory | Utilization and headroom trend";
            break;
        case PerformanceView::Disk:
            title = L"Disk | Read/Write throughput and latency";
            break;
        case PerformanceView::Wifi:
        {
            const auto *iface = GetActiveNetworkInterface();
            if (!iface)
            {
                title = L"Wi-Fi | No active adapter";
                break;
            }

            const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
            size_t wifiOrdinal = 0;
            for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
            {
                if (networkInterfaces_[i].ifType == IF_TYPE_IEEE80211)
                {
                    ++wifiOrdinal;
                }
            }

            title = L"Wi-Fi " + std::to_wstring(wifiOrdinal > 0 ? wifiOrdinal - 1 : 0);
            if (!iface->adapterName.empty())
            {
                title += L" | ";
                title += iface->adapterName;
            }
            break;
        }
        case PerformanceView::Ethernet:
        {
            const auto *iface = GetActiveNetworkInterface();
            if (!iface)
            {
                title = L"Network | No active adapter";
                break;
            }

            const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
            size_t ordinal = 0;
            for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
            {
                if (networkInterfaces_[i].ifType != IF_TYPE_IEEE80211)
                {
                    ++ordinal;
                }
            }

            title = NetworkTypeLabel(iface->ifType) + L" " + std::to_wstring(ordinal > 0 ? ordinal - 1 : 0);
            if (!iface->adapterName.empty())
            {
                title += L" | ";
                title += iface->adapterName;
            }
            break;
        }
        case PerformanceView::Gpu:
        {
            const auto *gpu = GetActiveGpu();
            if (!gpu)
            {
                title = L"GPU | No adapter detected";
                break;
            }

            const size_t gpuIndex = static_cast<size_t>(gpu - gpuDevices_.data());
            title = L"GPU " + std::to_wstring(gpuIndex + 1);
            if (!gpu->name.empty())
            {
                title += L" | ";
                title += gpu->name;
            }
            break;
        }
        }
        SetWindowTextW(performancePlaceholder_, title.c_str());

        SetWindowTextW(perfDetails_, BuildPerformanceDetailsText().c_str());

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
            std::wstring title;
            const wchar_t *unit = L"";
            COLORREF lineColor = RGB(37, 99, 235);
            double latest = 0.0;
            double maxValue = 100.0;
            bool showPercent = false;
            bool showCoreOverlay = false;
            bool wholeNumberTicks = false;
        };

        GraphConfig cfg{};
        switch (draw->CtlID)
        {
        case kIdPerfGraphCpu:
            if (activePerformanceView_ == PerformanceView::Gpu)
            {
                cfg.history = &gpu3dHistory_;
                cfg.title = L"GPU 3D";
                cfg.unit = L"%";
                cfg.lineColor = RGB(96, 118, 255);
                cfg.latest = gpu3dPercent_;
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            else
            {
                cfg.history = &cpuHistory_;
                cfg.title = L"CPU Total";
                cfg.unit = L"%";
                cfg.lineColor = RGB(30, 120, 230);
                cfg.latest = totalCpuPercent_;
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            break;
        case kIdPerfGraphMemory:
            if (activePerformanceView_ == PerformanceView::Gpu)
            {
                cfg.history = &gpuCopyHistory_;
                cfg.title = L"GPU Copy";
                cfg.unit = L"%";
                cfg.lineColor = RGB(53, 189, 126);
                cfg.latest = gpuCopyPercent_;
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            else
            {
                cfg.history = &memoryUtilizationHistory_;
                cfg.title = L"Memory Utilization";
                cfg.unit = L"%";
                cfg.lineColor = RGB(16, 166, 115);
                cfg.latest = memoryPercent_;
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            break;
        case kIdPerfGraphGpu:
            if (activePerformanceView_ == PerformanceView::Memory)
            {
                cfg.history = &memoryHeadroomHistory_;
                cfg.title = L"Memory Headroom";
                cfg.unit = L"%";
                cfg.lineColor = RGB(44, 132, 220);
                cfg.latest = (std::clamp)(100.0 - memoryPercent_, 0.0, 100.0);
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            else if (activePerformanceView_ == PerformanceView::Gpu)
            {
                cfg.history = &gpuDedicatedHistory_;
                cfg.title = L"GPU Dedicated Memory";
                cfg.unit = L"GB";
                cfg.lineColor = RGB(166, 81, 223);
                cfg.latest = gpuDedicatedUsedGb_;
                cfg.maxValue = (std::max)(0.5, gpuDedicatedGb_);
            }
            else
            {
                cfg.history = &gpuHistory_;
                cfg.title = L"GPU";
                cfg.unit = L"%";
                cfg.lineColor = RGB(148, 73, 224);
                cfg.latest = gpuPercent_;
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            break;
        case kIdPerfGraphUpload:
            if (activePerformanceView_ == PerformanceView::Gpu)
            {
                cfg.history = &gpuDecodeHistory_;
                cfg.title = L"GPU Video Decode";
                cfg.unit = L"%";
                cfg.lineColor = RGB(236, 150, 14);
                cfg.latest = gpuDecodePercent_;
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            else if (activePerformanceView_ == PerformanceView::Disk)
            {
                cfg.history = &diskReadHistory_;
                cfg.title = L"Disk Read";
                cfg.unit = L"MB/s";
                cfg.lineColor = RGB(27, 145, 102);
                cfg.latest = diskReadMBps_;
                cfg.maxValue = (std::max)(1.0, diskReadScaleMBps_);
            }
            else if (activePerformanceView_ == PerformanceView::Wifi)
            {
                const auto *iface = GetActiveNetworkInterface();
                if (iface)
                {
                    const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
                    size_t wifiOrdinal = 0;
                    for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
                    {
                        if (networkInterfaces_[i].ifType == IF_TYPE_IEEE80211)
                        {
                            ++wifiOrdinal;
                        }
                    }

                    cfg.history = &iface->uploadHistory;
                    std::wstring title = L"Wi-Fi ";
                    title += std::to_wstring(wifiOrdinal > 0 ? wifiOrdinal - 1 : 0);
                    title += L" Upload";
                    if (!iface->adapterName.empty())
                    {
                        title += L" | ";
                        title += iface->adapterName;
                    }
                    cfg.title = title;
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(236, 150, 14);
                    cfg.latest = iface->uploadMbps;
                    cfg.maxValue = (std::max)(0.5, iface->uploadScaleMbps);
                }
                else
                {
                    cfg.history = &wifiUploadHistory_;
                    cfg.title = L"Wi-Fi Upload";
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(236, 150, 14);
                    cfg.latest = wifiUploadMbps_;
                    cfg.maxValue = (std::max)(0.5, wifiUploadScaleMbps_);
                }
            }
            else if (activePerformanceView_ == PerformanceView::Ethernet)
            {
                const auto *iface = GetActiveNetworkInterface();
                if (iface)
                {
                    const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
                    size_t ordinal = 0;
                    for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
                    {
                        if (networkInterfaces_[i].ifType != IF_TYPE_IEEE80211)
                        {
                            ++ordinal;
                        }
                    }

                    cfg.history = &iface->uploadHistory;
                    std::wstring title = NetworkTypeLabel(iface->ifType);
                    title += L" ";
                    title += std::to_wstring(ordinal > 0 ? ordinal - 1 : 0);
                    title += L" Upload";
                    if (!iface->adapterName.empty())
                    {
                        title += L" | ";
                        title += iface->adapterName;
                    }
                    cfg.title = title;
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(236, 150, 14);
                    cfg.latest = iface->uploadMbps;
                    cfg.maxValue = (std::max)(0.5, iface->uploadScaleMbps);
                }
                else
                {
                    cfg.history = &ethernetUploadHistory_;
                    cfg.title = L"Ethernet Upload";
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(236, 150, 14);
                    cfg.latest = ethernetUploadMbps_;
                    cfg.maxValue = (std::max)(0.5, ethernetUploadScaleMbps_);
                }
            }
            else
            {
                cfg.history = &uploadHistory_;
                cfg.title = L"Network Upload";
                cfg.unit = L"Mbps";
                cfg.lineColor = RGB(236, 150, 14);
                cfg.latest = uploadMbps_;
                cfg.maxValue = (std::max)(0.5, uploadScaleMbps_);
            }
            break;
        case kIdPerfGraphDownload:
            if (activePerformanceView_ == PerformanceView::Gpu)
            {
                cfg.history = &gpuEncodeHistory_;
                cfg.title = L"GPU Video Encode";
                cfg.unit = L"%";
                cfg.lineColor = RGB(225, 76, 88);
                cfg.latest = gpuEncodePercent_;
                cfg.maxValue = 100.0;
                cfg.showPercent = true;
            }
            else if (activePerformanceView_ == PerformanceView::Disk)
            {
                cfg.history = &diskWriteHistory_;
                cfg.title = L"Disk Write";
                cfg.unit = L"MB/s";
                cfg.lineColor = RGB(208, 73, 86);
                cfg.latest = diskWriteMBps_;
                cfg.maxValue = (std::max)(1.0, diskWriteScaleMBps_);
            }
            else if (activePerformanceView_ == PerformanceView::Wifi)
            {
                const auto *iface = GetActiveNetworkInterface();
                if (iface)
                {
                    const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
                    size_t wifiOrdinal = 0;
                    for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
                    {
                        if (networkInterfaces_[i].ifType == IF_TYPE_IEEE80211)
                        {
                            ++wifiOrdinal;
                        }
                    }

                    cfg.history = &iface->downloadHistory;
                    std::wstring title = L"Wi-Fi ";
                    title += std::to_wstring(wifiOrdinal > 0 ? wifiOrdinal - 1 : 0);
                    title += L" Download";
                    if (!iface->adapterName.empty())
                    {
                        title += L" | ";
                        title += iface->adapterName;
                    }
                    cfg.title = title;
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(225, 76, 88);
                    cfg.latest = iface->downloadMbps;
                    cfg.maxValue = (std::max)(0.5, iface->downloadScaleMbps);
                }
                else
                {
                    cfg.history = &wifiDownloadHistory_;
                    cfg.title = L"Wi-Fi Download";
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(225, 76, 88);
                    cfg.latest = wifiDownloadMbps_;
                    cfg.maxValue = (std::max)(0.5, wifiDownloadScaleMbps_);
                }
            }
            else if (activePerformanceView_ == PerformanceView::Ethernet)
            {
                const auto *iface = GetActiveNetworkInterface();
                if (iface)
                {
                    const size_t selectedIndex = static_cast<size_t>(iface - networkInterfaces_.data());
                    size_t ordinal = 0;
                    for (size_t i = 0; i <= selectedIndex && i < networkInterfaces_.size(); ++i)
                    {
                        if (networkInterfaces_[i].ifType != IF_TYPE_IEEE80211)
                        {
                            ++ordinal;
                        }
                    }

                    cfg.history = &iface->downloadHistory;
                    std::wstring title = NetworkTypeLabel(iface->ifType);
                    title += L" ";
                    title += std::to_wstring(ordinal > 0 ? ordinal - 1 : 0);
                    title += L" Download";
                    if (!iface->adapterName.empty())
                    {
                        title += L" | ";
                        title += iface->adapterName;
                    }
                    cfg.title = title;
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(225, 76, 88);
                    cfg.latest = iface->downloadMbps;
                    cfg.maxValue = (std::max)(0.5, iface->downloadScaleMbps);
                }
                else
                {
                    cfg.history = &ethernetDownloadHistory_;
                    cfg.title = L"Ethernet Download";
                    cfg.unit = L"Mbps";
                    cfg.lineColor = RGB(225, 76, 88);
                    cfg.latest = ethernetDownloadMbps_;
                    cfg.maxValue = (std::max)(0.5, ethernetDownloadScaleMbps_);
                }
            }
            else
            {
                cfg.history = &downloadHistory_;
                cfg.title = L"Network Download";
                cfg.unit = L"Mbps";
                cfg.lineColor = RGB(225, 76, 88);
                cfg.latest = downloadMbps_;
                cfg.maxValue = (std::max)(0.5, downloadScaleMbps_);
            }
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

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(216, 225, 239));
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
        DrawTextW(dc, cfg.title.c_str(), -1, &titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

        std::wstring latestLabel;
        if (cfg.showPercent)
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

        HBRUSH plotBrush = CreateSolidBrush(RGB(248, 251, 255));
        FillRect(dc, &plot, plotBrush);
        DeleteObject(plotBrush);

        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(227, 233, 244));
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
            else if (cfg.wholeNumberTicks)
            {
                yLabel = std::to_wstring(static_cast<int>(tickValue + 0.5));
                yLabel += L" ";
                yLabel += cfg.unit;
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

        auto drawCardChart = [&](const RECT &card,
                                 const std::wstring &title,
                                 const std::deque<double> &history,
                                 double latest,
                                 double maxValue,
                                 COLORREF lineColor,
                                 const wchar_t *unit,
                                 bool showPercent)
        {
            HBRUSH tileBrush = CreateSolidBrush(RGB(248, 251, 255));
            FillRect(dc, &card, tileBrush);
            DeleteObject(tileBrush);

            HPEN tileBorder = CreatePen(PS_SOLID, 1, RGB(216, 225, 239));
            HPEN pen = reinterpret_cast<HPEN>(SelectObject(dc, tileBorder));
            HBRUSH brush = reinterpret_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
            Rectangle(dc, card.left, card.top, card.right, card.bottom);
            SelectObject(dc, brush);
            SelectObject(dc, pen);
            DeleteObject(tileBorder);

            RECT titleRect = card;
            titleRect.left += 8;
            titleRect.top += 5;
            DrawTextW(dc, title.c_str(), -1, &titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

            std::wstring valueText;
            if (showPercent)
            {
                valueText = std::to_wstring(static_cast<int>(latest + 0.5));
                valueText += L"%";
            }
            else
            {
                valueText = FormatAxisValue(latest, unit);
            }

            RECT valueRect = card;
            valueRect.right -= 8;
            valueRect.top += 5;
            DrawTextW(dc, valueText.c_str(), -1, &valueRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);

            RECT plot = card;
            plot.left += 8;
            plot.right -= 8;
            plot.top += 24;
            plot.bottom -= 8;

            const int plotWidth = (std::max)(1, static_cast<int>(plot.right - plot.left));
            const int plotHeight = (std::max)(1, static_cast<int>(plot.bottom - plot.top));
            const double safeMax = (std::max)(0.001, maxValue);

            HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(227, 233, 244));
            pen = reinterpret_cast<HPEN>(SelectObject(dc, gridPen));
            for (int line = 1; line <= 3; ++line)
            {
                const int y = plot.top + (plotHeight * line) / 4;
                MoveToEx(dc, plot.left, y, nullptr);
                LineTo(dc, plot.right, y);
            }
            SelectObject(dc, pen);
            DeleteObject(gridPen);

            if (history.size() >= 2)
            {
                std::vector<POINT> points;
                const int n = static_cast<int>(history.size());
                points.reserve(n);
                for (int i = 0; i < n; ++i)
                {
                    const double sample = (std::clamp)(history[i], 0.0, safeMax);
                    const int x = plot.left + ((plotWidth - 1) * i) / (n - 1);
                    const int y = plot.bottom - static_cast<int>((sample / safeMax) * (plotHeight - 1));
                    points.push_back(POINT{x, y});
                }

                std::vector<POINT> area;
                area.reserve(points.size() + 2);
                area.push_back(POINT{points.front().x, plot.bottom});
                area.insert(area.end(), points.begin(), points.end());
                area.push_back(POINT{points.back().x, plot.bottom});

                HBRUSH areaBrush = CreateSolidBrush(BlendColor(lineColor, RGB(255, 255, 255), 0.82));
                HBRUSH prevBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, areaBrush));
                HPEN nullPen = reinterpret_cast<HPEN>(SelectObject(dc, GetStockObject(NULL_PEN)));
                Polygon(dc, area.data(), static_cast<int>(area.size()));
                SelectObject(dc, nullPen);
                SelectObject(dc, prevBrush);
                DeleteObject(areaBrush);

                HPEN linePen = CreatePen(PS_SOLID, 1, lineColor);
                HPEN prevPen = reinterpret_cast<HPEN>(SelectObject(dc, linePen));
                MoveToEx(dc, points.front().x, points.front().y, nullptr);
                for (size_t i = 1; i < points.size(); ++i)
                {
                    LineTo(dc, points[i].x, points[i].y);
                }
                SelectObject(dc, prevPen);
                DeleteObject(linePen);
            }
        };

        struct CardItem
        {
            std::wstring title;
            const std::deque<double> *history = nullptr;
            double latest = 0.0;
            double maxValue = 100.0;
            COLORREF color = RGB(30, 120, 230);
            const wchar_t *unit = L"%";
            bool percent = false;
        };

        auto renderCardCollection = [&](const std::wstring &heading,
                                        const std::vector<CardItem> &cards,
                                        int *scrollOffsetState,
                                        int *contentHeightState)
        {
            RECT titleRect = local;
            titleRect.left += 10;
            titleRect.top += 5;
            DrawTextW(dc, heading.c_str(), -1, &titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

            if (cards.empty())
            {
                if (scrollOffsetState)
                {
                    *scrollOffsetState = 0;
                }
                if (contentHeightState)
                {
                    *contentHeightState = 0;
                }

                RECT emptyRect = local;
                emptyRect.left += 10;
                emptyRect.top += 30;
                SetTextColor(dc, RGB(104, 118, 141));
                DrawTextW(dc, L"No live graphs available for this section.", -1, &emptyRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
                return;
            }

            const int topMargin = 30;
            const int bottomMargin = 8;
            const int gap = 10;
            const int cols = width >= 280 ? 2 : 1;
            const int cardWidth = (std::max)(100, (width - 16 - (cols > 1 ? gap : 0)) / cols);
            const int cardHeight = 132;
            const int rows = static_cast<int>((cards.size() + cols - 1) / cols);
            const int contentHeight = rows * (cardHeight + gap) - gap + topMargin + bottomMargin;
            const int viewportHeight = (std::max)(1, height - topMargin - bottomMargin);
            const int maxOffset = (std::max)(0, contentHeight - viewportHeight);

            int scrollOffset = scrollOffsetState ? (std::clamp)(*scrollOffsetState, 0, maxOffset) : 0;
            if (scrollOffsetState)
            {
                *scrollOffsetState = scrollOffset;
            }
            if (contentHeightState)
            {
                *contentHeightState = contentHeight;
            }

            for (size_t i = 0; i < cards.size(); ++i)
            {
                const int row = static_cast<int>(i / cols);
                const int col = static_cast<int>(i % cols);

                RECT card{
                    8 + col * (cardWidth + gap),
                    topMargin + row * (cardHeight + gap) - scrollOffset,
                    8 + col * (cardWidth + gap) + cardWidth,
                    topMargin + row * (cardHeight + gap) - scrollOffset + cardHeight};

                if (card.bottom < topMargin || card.top > height - bottomMargin)
                {
                    continue;
                }

                if (!cards[i].history)
                {
                    continue;
                }

                drawCardChart(card, cards[i].title, *cards[i].history, cards[i].latest, cards[i].maxValue, cards[i].color, cards[i].unit, cards[i].percent);
            }

            if (maxOffset > 0)
            {
                RECT scrollTrack{width - 5, topMargin, width - 3, height - bottomMargin};
                HBRUSH trackBrush = CreateSolidBrush(RGB(228, 235, 245));
                FillRect(dc, &scrollTrack, trackBrush);
                DeleteObject(trackBrush);

                const int trackHeight = (std::max)(1, static_cast<int>(scrollTrack.bottom - scrollTrack.top));
                const int thumbHeight = (std::max)(20, (trackHeight * viewportHeight) / (std::max)(viewportHeight, contentHeight));
                const int thumbTop = scrollTrack.top + ((trackHeight - thumbHeight) * scrollOffset) / (std::max)(1, maxOffset);

                RECT thumb{scrollTrack.left, thumbTop, scrollTrack.right, thumbTop + thumbHeight};
                HBRUSH thumbBrush = CreateSolidBrush(RGB(155, 170, 196));
                FillRect(dc, &thumb, thumbBrush);
                DeleteObject(thumbBrush);
            }
        };

        auto *mutableSelf = const_cast<MainWindow *>(this);

        if (activePerformanceView_ == PerformanceView::All ||
            activePerformanceView_ == PerformanceView::Gpu)
        {
            constexpr COLORREF kNetworkUpColor = RGB(236, 150, 14);
            constexpr COLORREF kNetworkDownColor = RGB(225, 76, 88);
            constexpr COLORREF kGpuPalette[] = {
                RGB(96, 118, 255),
                RGB(53, 189, 126),
                RGB(236, 150, 14),
                RGB(225, 76, 88),
                RGB(52, 144, 236),
                RGB(166, 81, 223)};

            std::vector<CardItem> cards;
            std::wstring heading;

            if (activePerformanceView_ == PerformanceView::All)
            {
                heading = L"All Graphs (scroll with mouse wheel)";

                cards.push_back({L"CPU Total", &cpuHistory_, totalCpuPercent_, 100.0, RGB(30, 120, 230), L"%", true});
                cards.push_back({L"Memory In Use", &memoryHistory_, memoryUsedGb_, (std::max)(1.0, memoryAxisTopGb_), RGB(16, 166, 115), L"GB", false});
                cards.push_back({L"Memory Available", &memoryAvailableHistory_, memoryAvailableGb_, (std::max)(1.0, memoryAxisTopGb_), RGB(44, 132, 220), L"GB", false});
                cards.push_back({L"Disk Read", &diskReadHistory_, diskReadMBps_, (std::max)(1.0, diskReadScaleMBps_), RGB(27, 145, 102), L"MB/s", false});
                cards.push_back({L"Disk Write", &diskWriteHistory_, diskWriteMBps_, (std::max)(1.0, diskWriteScaleMBps_), RGB(208, 73, 86), L"MB/s", false});

                for (size_t i = 0; i < networkInterfaces_.size(); ++i)
                {
                    const auto &iface = networkInterfaces_[i];
                    std::wstring base = iface.adapterName.empty() ? (L"Interface " + std::to_wstring(i)) : iface.adapterName;
                    cards.push_back({base + L" Upload", &iface.uploadHistory, iface.uploadMbps, (std::max)(0.5, iface.uploadScaleMbps), kNetworkUpColor, L"Mbps", false});
                    cards.push_back({base + L" Download", &iface.downloadHistory, iface.downloadMbps, (std::max)(0.5, iface.downloadScaleMbps), kNetworkDownColor, L"Mbps", false});
                }

                for (size_t i = 0; i < gpuDevices_.size(); ++i)
                {
                    const auto &gpu = gpuDevices_[i];
                    const COLORREF color = kGpuPalette[i % (sizeof(kGpuPalette) / sizeof(kGpuPalette[0]))];
                    cards.push_back({L"GPU " + std::to_wstring(i + 1) + L" Util", &gpu.utilizationHistory, gpu.utilizationPercent, 100.0, color, L"%", true});
                }
            }
            else
            {
                const auto *gpu = GetActiveGpu();
                if (!gpu)
                {
                    heading = L"GPU | No adapter detected";
                }
                else
                {
                    const size_t gpuIndex = static_cast<size_t>(gpu - gpuDevices_.data());
                    heading = L"GPU ";
                    heading += std::to_wstring(gpuIndex + 1);
                    if (!gpu->name.empty())
                    {
                        heading += L" | ";
                        heading += gpu->name;
                    }

                    const COLORREF utilColor = kGpuPalette[gpuIndex % (sizeof(kGpuPalette) / sizeof(kGpuPalette[0]))];
                    const COLORREF memoryColor = kGpuPalette[(gpuIndex + 1) % (sizeof(kGpuPalette) / sizeof(kGpuPalette[0]))];
                    cards.push_back({L"Utilization", &gpu->utilizationHistory, gpu->utilizationPercent, 100.0, utilColor, L"%", true});
                    cards.push_back({L"Dedicated Memory", &gpu->dedicatedHistory, gpu->dedicatedUsedGb, (std::max)(0.5, gpu->dedicatedGb), memoryColor, L"GB", false});
                    cards.push_back({L"Shared Memory", &gpu->sharedHistory, gpu->sharedUsedGb, (std::max)(0.5, gpu->sharedGb), RGB(52, 144, 236), L"GB", false});
                }
            }

            renderCardCollection(heading, cards, &mutableSelf->perfAllScrollOffset_, &mutableSelf->perfAllContentHeight_);

            BitBlt(targetDc, rc.left, rc.top, width, height, dc, 0, 0, SRCCOPY);
            SelectObject(dc, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(dc);
            return;
        }

        if (activePerformanceView_ == PerformanceView::Cpu && cpuGraphMode_ == CpuGraphMode::PerCore)
        {
            constexpr COLORREF kCorePalette[] = {
                RGB(68, 128, 245), RGB(25, 173, 123), RGB(130, 102, 246), RGB(229, 140, 28),
                RGB(210, 84, 109), RGB(23, 163, 203), RGB(163, 119, 230), RGB(94, 138, 40)};

            std::vector<CardItem> cards;
            cards.reserve(performanceCoreHistory_.size());

            for (size_t idx = 0; idx < performanceCoreHistory_.size(); ++idx)
            {
                double usageValue = 0.0;
                if (idx < performanceCoreUsage_.size())
                {
                    usageValue = performanceCoreUsage_[idx];
                }

                const COLORREF color = kCorePalette[idx % (sizeof(kCorePalette) / sizeof(kCorePalette[0]))];
                cards.push_back({L"Core " + std::to_wstring(idx), &performanceCoreHistory_[idx], usageValue, 100.0, color, L"%", true});
            }

            renderCardCollection(
                L"Per-Core CPU Graphs (scroll with mouse wheel)",
                cards,
                &mutableSelf->perfCpuCoreScrollOffset_,
                &mutableSelf->perfCpuCoreContentHeight_);
        }
        else
        {
            mutableSelf->perfCpuCoreScrollOffset_ = 0;
            mutableSelf->perfCpuCoreContentHeight_ = 0;
            RECT infoRect = local;
            infoRect.left += 10;
            infoRect.top += 8;
            SetTextColor(dc, RGB(104, 118, 141));
            DrawTextW(dc, L"Per-core CPU view is hidden in Single Graph mode.", -1, &infoRect, DT_LEFT | DT_TOP | DT_SINGLELINE);
        }

        BitBlt(targetDc, rc.left, rc.top, width, height, dc, 0, 0, SRCCOPY);

        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

} // namespace utm::ui
