#include <winsock2.h>

#include "ui/NetworkPanel.h"

#include "ui/MainWindow.h"

#include "tools/process/ProcessActions.h"

#include <commctrl.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cwctype>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr UINT kIdNetworkSearchEdit = 1062;
    constexpr UINT kIdNetworkModeCombo = 1064;
    constexpr UINT kIdNetworkList = 1065;
    constexpr UINT kIdNetworkRefreshButton = 1066;
    constexpr UINT kIdNetworkTerminateButton = 1067;
    constexpr UINT kIdNetworkCloseButton = 1068;

    std::wstring ToLowerNetwork(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitiveNetwork(const std::wstring &text, const std::wstring &loweredQuery)
    {
        if (loweredQuery.empty())
        {
            return true;
        }

        const std::wstring loweredText = ToLowerNetwork(text);
        return loweredText.find(loweredQuery) != std::wstring::npos;
    }

    bool IsBlockedProcessTarget(DWORD pid, DWORD selfPid)
    {
        return pid == 0 || pid == 4 || pid == selfPid;
    }

    std::wstring Win32ErrorToTextNetwork(DWORD error)
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

    ULONG QueryIpTableBuffer(const std::function<ULONG(PVOID, DWORD *)> &query, std::vector<BYTE> &buffer)
    {
        DWORD size = 0;
        ULONG result = query(nullptr, &size);
        if (result == ERROR_INSUFFICIENT_BUFFER && size > 0)
        {
            buffer.resize(size);
            return query(buffer.data(), &size);
        }

        if (result == NO_ERROR && size > 0)
        {
            buffer.resize(size);
            return query(buffer.data(), &size);
        }

        buffer.clear();
        return result;
    }

    USHORT HostPortFromDword(DWORD networkPort)
    {
        return ntohs(static_cast<u_short>(networkPort));
    }

    std::wstring FormatIpv4(DWORD networkAddress)
    {
        IN_ADDR address{};
        address.S_un.S_addr = networkAddress;

        wchar_t text[64]{};
        if (!InetNtopW(AF_INET, &address, text, static_cast<DWORD>(sizeof(text) / sizeof(text[0]))))
        {
            return L"0.0.0.0";
        }

        return text;
    }

    std::wstring BuildEndpointText(const std::wstring &address, USHORT port, bool ipv6)
    {
        if (address.empty())
        {
            return L"N/A";
        }

        const std::wstring normalizedAddress = (address == L"0.0.0.0" || address == L"::") ? L"*" : address;
        if (port == 0)
        {
            if (ipv6 && normalizedAddress != L"*")
            {
                return L"[" + normalizedAddress + L"]:*";
            }
            return normalizedAddress + L":*";
        }

        if (ipv6)
        {
            return L"[" + normalizedAddress + L"]:" + std::to_wstring(port);
        }

        return normalizedAddress + L":" + std::to_wstring(port);
    }

    std::wstring TcpStateToText(DWORD state)
    {
        switch (state)
        {
        case MIB_TCP_STATE_CLOSED:
            return L"Closed";
        case MIB_TCP_STATE_LISTEN:
            return L"Listening";
        case MIB_TCP_STATE_SYN_SENT:
            return L"SYN Sent";
        case MIB_TCP_STATE_SYN_RCVD:
            return L"SYN Received";
        case MIB_TCP_STATE_ESTAB:
            return L"Established";
        case MIB_TCP_STATE_FIN_WAIT1:
            return L"FIN Wait 1";
        case MIB_TCP_STATE_FIN_WAIT2:
            return L"FIN Wait 2";
        case MIB_TCP_STATE_CLOSE_WAIT:
            return L"Close Wait";
        case MIB_TCP_STATE_CLOSING:
            return L"Closing";
        case MIB_TCP_STATE_LAST_ACK:
            return L"Last ACK";
        case MIB_TCP_STATE_TIME_WAIT:
            return L"Time Wait";
        case MIB_TCP_STATE_DELETE_TCB:
            return L"Delete TCB";
        default:
            return L"Unknown";
        }
    }

} // namespace

namespace utm::ui
{

    void NetworkPanel::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool NetworkPanel::HandleCommand(UINT id, UINT code)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleNetworkCommand(id, code);
    }

    bool NetworkPanel::HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleNetworkNotify(hdr, lParam, result);
    }

    int MainWindow::SelectedNetworkIndex() const
    {
        if (!networkList_)
        {
            return -1;
        }

        const int selectedRow = ListView_GetNextItem(networkList_, -1, LVNI_SELECTED);
        if (selectedRow < 0 || static_cast<size_t>(selectedRow) >= networkVisibleRows_.size())
        {
            return -1;
        }

        const size_t connectionIndex = networkVisibleRows_[static_cast<size_t>(selectedRow)];
        if (connectionIndex >= networkConnections_.size())
        {
            return -1;
        }

        return static_cast<int>(connectionIndex);
    }

    void MainWindow::RefreshNetworkActionState()
    {
        if (!networkTerminateButton_ || !networkCloseButton_)
        {
            return;
        }

        const int selected = SelectedNetworkIndex();
        if (selected < 0)
        {
            SetWindowTextW(networkTerminateButton_, L"Terminate Owner Process");
            SetWindowTextW(networkCloseButton_, L"Close TCP Connection");
            EnableWindow(networkTerminateButton_, FALSE);
            EnableWindow(networkCloseButton_, FALSE);

            if (networkStatus_)
            {
                std::wstringstream summary;
                summary << L"Showing " << networkVisibleRows_.size() << L" of " << networkConnections_.size() << L" endpoints.";
                if (!networkFilterText_.empty())
                {
                    summary << L" Filter: " << networkFilterText_;
                }
                SetWindowTextW(networkStatus_, summary.str().c_str());
            }
            return;
        }

        const auto &connection = networkConnections_[static_cast<size_t>(selected)];
        const DWORD selfPid = GetCurrentProcessId();
        const bool allowTerminate = !IsBlockedProcessTarget(connection.pid, selfPid);

        EnableWindow(networkTerminateButton_, allowTerminate ? TRUE : FALSE);
        EnableWindow(networkCloseButton_, connection.canClose ? TRUE : FALSE);

        if (networkStatus_)
        {
            std::wstringstream detail;
            detail << L"Selected: " << connection.protocol << L" " << connection.localEndpoint
                   << L" -> " << connection.remoteEndpoint
                   << L" | PID " << connection.pid
                   << L" (" << connection.processName << L")"
                   << L" | Visible " << networkVisibleRows_.size() << L"/" << networkConnections_.size();

            if (!allowTerminate)
            {
                detail << L" | Process termination blocked";
            }
            if (!connection.canClose)
            {
                detail << L" | Close unsupported";
            }

            SetWindowTextW(networkStatus_, detail.str().c_str());
        }
    }

    void MainWindow::ApplyNetworkFilterToList(bool preserveSelection)
    {
        if (!networkList_)
        {
            return;
        }

        std::wstring selectedKey;
        if (preserveSelection)
        {
            const int selected = SelectedNetworkIndex();
            if (selected >= 0)
            {
                selectedKey = networkConnections_[static_cast<size_t>(selected)].rowKey;
            }
        }

        const int previousTopIndex = ListView_GetTopIndex(networkList_);
        const std::wstring loweredQuery = ToLowerNetwork(networkFilterText_);

        SendMessageW(networkList_, WM_SETREDRAW, FALSE, 0);

        ListView_DeleteAllItems(networkList_);
        networkVisibleRows_.clear();
        networkVisibleRows_.reserve(networkConnections_.size());

        int selectedRow = -1;

        for (size_t i = 0; i < networkConnections_.size(); ++i)
        {
            const auto &connection = networkConnections_[i];

            bool passMode = false;
            switch (activeNetworkFilterMode_)
            {
            case NetworkFilterMode::All:
                passMode = true;
                break;
            case NetworkFilterMode::Tcp:
                passMode = connection.isTcp;
                break;
            case NetworkFilterMode::Udp:
                passMode = !connection.isTcp;
                break;
            case NetworkFilterMode::Listening:
                passMode = connection.isListening;
                break;
            case NetworkFilterMode::Established:
                passMode = connection.isEstablished;
                break;
            }

            if (!passMode)
            {
                continue;
            }

            const std::wstring pidText = std::to_wstring(connection.pid);
            const bool passQuery = loweredQuery.empty() ||
                                   ContainsInsensitiveNetwork(connection.protocol, loweredQuery) ||
                                   ContainsInsensitiveNetwork(connection.addressFamily, loweredQuery) ||
                                   ContainsInsensitiveNetwork(connection.localEndpoint, loweredQuery) ||
                                   ContainsInsensitiveNetwork(connection.remoteEndpoint, loweredQuery) ||
                                   ContainsInsensitiveNetwork(connection.stateText, loweredQuery) ||
                                   ContainsInsensitiveNetwork(connection.processName, loweredQuery) ||
                                   ContainsInsensitiveNetwork(pidText, loweredQuery);

            if (!passQuery)
            {
                continue;
            }

            const int row = static_cast<int>(networkVisibleRows_.size());
            networkVisibleRows_.push_back(i);

            std::wstring protocolColumn = connection.protocol + L" / " + connection.addressFamily;

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(protocolColumn.c_str());

            const int inserted = ListView_InsertItem(networkList_, &item);
            if (inserted < 0)
            {
                continue;
            }

            ListView_SetItemText(networkList_, inserted, 1, const_cast<LPWSTR>(connection.localEndpoint.c_str()));
            ListView_SetItemText(networkList_, inserted, 2, const_cast<LPWSTR>(connection.remoteEndpoint.c_str()));
            ListView_SetItemText(networkList_, inserted, 3, const_cast<LPWSTR>(connection.stateText.c_str()));
            ListView_SetItemText(networkList_, inserted, 4, const_cast<LPWSTR>(connection.processName.c_str()));
            ListView_SetItemText(networkList_, inserted, 5, const_cast<LPWSTR>(pidText.c_str()));

            if (!selectedKey.empty() && connection.rowKey == selectedKey)
            {
                selectedRow = inserted;
            }
        }

        if (selectedRow < 0 && !networkVisibleRows_.empty())
        {
            selectedRow = 0;
        }

        if (selectedRow >= 0)
        {
            ListView_SetItemState(networkList_, selectedRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(networkList_, selectedRow, FALSE);
        }

        const int visibleCount = ListView_GetItemCount(networkList_);
        if (visibleCount > 0 && previousTopIndex > 0)
        {
            const int targetTop = (std::min)(previousTopIndex, visibleCount - 1);
            ListView_EnsureVisible(networkList_, targetTop, FALSE);
        }

        SendMessageW(networkList_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(networkList_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);

        RefreshNetworkActionState();
    }

    void MainWindow::RefreshNetworkInventory(bool preserveSelection)
    {
        if (!networkList_)
        {
            return;
        }

        std::unordered_map<DWORD, std::wstring> processByPid;
        processByPid.reserve(snapshot_.processes.size());
        for (const auto &process : snapshot_.processes)
        {
            processByPid.emplace(process.pid, process.imageName);
        }

        auto processNameForPid = [&](DWORD pid)
        {
            const auto it = processByPid.find(pid);
            if (it != processByPid.end() && !it->second.empty())
            {
                return it->second;
            }

            if (pid == 0)
            {
                return std::wstring(L"System Idle Process");
            }

            if (pid == 4)
            {
                return std::wstring(L"System");
            }

            return std::wstring(L"Unknown Process");
        };

        std::vector<NetworkConnectionEntry> refreshed;
        ULONG lastFailure = NO_ERROR;

        std::vector<BYTE> tcp4Buffer;
        ULONG tcp4Result = QueryIpTableBuffer(
            [](PVOID table, DWORD *size)
            {
                return GetExtendedTcpTable(table, size, TRUE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
            },
            tcp4Buffer);

        if (tcp4Result == NO_ERROR && !tcp4Buffer.empty())
        {
            const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(tcp4Buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i)
            {
                const auto &row = table->table[i];

                NetworkConnectionEntry entry{};
                entry.protocol = L"TCP";
                entry.addressFamily = L"IPv4";
                entry.pid = row.dwOwningPid;
                entry.processName = processNameForPid(row.dwOwningPid);
                entry.stateText = TcpStateToText(row.dwState);
                entry.isTcp = true;
                entry.isListening = row.dwState == MIB_TCP_STATE_LISTEN;
                entry.isEstablished = row.dwState == MIB_TCP_STATE_ESTAB;
                entry.closeSupported = true;
                entry.canClose = row.dwState != MIB_TCP_STATE_LISTEN && row.dwState != MIB_TCP_STATE_CLOSED;

                const USHORT localPort = HostPortFromDword(row.dwLocalPort);
                const USHORT remotePort = HostPortFromDword(row.dwRemotePort);

                const std::wstring localAddress = FormatIpv4(row.dwLocalAddr);
                const std::wstring remoteAddress = entry.isListening ? L"*" : FormatIpv4(row.dwRemoteAddr);

                entry.localEndpoint = BuildEndpointText(localAddress, localPort, false);
                entry.remoteEndpoint = entry.isListening ? L"*:*" : BuildEndpointText(remoteAddress, remotePort, false);

                entry.tcpLocalAddrNetwork = row.dwLocalAddr;
                entry.tcpRemoteAddrNetwork = row.dwRemoteAddr;
                entry.tcpLocalPortNetwork = row.dwLocalPort;
                entry.tcpRemotePortNetwork = row.dwRemotePort;

                entry.rowKey = entry.protocol + L"|" + entry.addressFamily + L"|" +
                               entry.localEndpoint + L"|" + entry.remoteEndpoint + L"|" + std::to_wstring(entry.pid);

                refreshed.push_back(std::move(entry));
            }
        }
        else if (tcp4Result != NO_ERROR)
        {
            lastFailure = tcp4Result;
        }

        std::vector<BYTE> udp4Buffer;
        ULONG udp4Result = QueryIpTableBuffer(
            [](PVOID table, DWORD *size)
            {
                return GetExtendedUdpTable(table, size, TRUE, AF_INET, UDP_TABLE_OWNER_PID, 0);
            },
            udp4Buffer);

        if (udp4Result == NO_ERROR && !udp4Buffer.empty())
        {
            const auto *table = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID *>(udp4Buffer.data());
            for (DWORD i = 0; i < table->dwNumEntries; ++i)
            {
                const auto &row = table->table[i];

                NetworkConnectionEntry entry{};
                entry.protocol = L"UDP";
                entry.addressFamily = L"IPv4";
                entry.pid = row.dwOwningPid;
                entry.processName = processNameForPid(row.dwOwningPid);
                entry.stateText = L"Stateless";
                entry.isTcp = false;
                entry.isListening = true;
                entry.isEstablished = false;
                entry.closeSupported = false;
                entry.canClose = false;

                const USHORT localPort = HostPortFromDword(row.dwLocalPort);
                const std::wstring localAddress = FormatIpv4(row.dwLocalAddr);
                entry.localEndpoint = BuildEndpointText(localAddress, localPort, false);
                entry.remoteEndpoint = L"-";

                entry.rowKey = entry.protocol + L"|" + entry.addressFamily + L"|" +
                               entry.localEndpoint + L"|" + std::to_wstring(entry.pid);

                refreshed.push_back(std::move(entry));
            }
        }
        else if (udp4Result != NO_ERROR && lastFailure == NO_ERROR)
        {
            lastFailure = udp4Result;
        }

        std::sort(refreshed.begin(), refreshed.end(), [](const NetworkConnectionEntry &left, const NetworkConnectionEntry &right)
                  {
                      const std::wstring leftProto = ToLowerNetwork(left.protocol);
                      const std::wstring rightProto = ToLowerNetwork(right.protocol);
                      if (leftProto != rightProto)
                      {
                          return leftProto < rightProto;
                      }

                      const std::wstring leftLocal = ToLowerNetwork(left.localEndpoint);
                      const std::wstring rightLocal = ToLowerNetwork(right.localEndpoint);
                      if (leftLocal != rightLocal)
                      {
                          return leftLocal < rightLocal;
                      }

                      if (left.pid != right.pid)
                      {
                          return left.pid < right.pid;
                      }

                      return left.rowKey < right.rowKey; });

        networkConnections_ = std::move(refreshed);
        lastNetworkRefreshTickMs_ = GetTickCount64();

        ApplyNetworkFilterToList(preserveSelection);

        if (networkConnections_.empty() && lastFailure != NO_ERROR && networkStatus_)
        {
            std::wstring text = L"Network endpoint scan failed. Error " + Win32ErrorToTextNetwork(lastFailure);
            SetWindowTextW(networkStatus_, text.c_str());
        }
    }

    bool MainWindow::TerminateSelectedNetworkProcess(std::wstring &errorText)
    {
        errorText.clear();

        const int selected = SelectedNetworkIndex();
        if (selected < 0)
        {
            errorText = L"Select a network endpoint first.";
            return false;
        }

        const auto &connection = networkConnections_[static_cast<size_t>(selected)];
        const DWORD selfPid = GetCurrentProcessId();
        if (IsBlockedProcessTarget(connection.pid, selfPid))
        {
            errorText = L"Terminating this process is blocked for safety.";
            return false;
        }

        if (!tools::process::ProcessActions::SmartTerminate(connection.pid, 2500, errorText))
        {
            if (errorText.empty())
            {
                errorText = L"Unable to terminate the selected process.";
            }
            return false;
        }

        return true;
    }

    bool MainWindow::CloseSelectedNetworkConnection(std::wstring &errorText)
    {
        errorText.clear();

        const int selected = SelectedNetworkIndex();
        if (selected < 0)
        {
            errorText = L"Select a TCP endpoint first.";
            return false;
        }

        const auto &connection = networkConnections_[static_cast<size_t>(selected)];
        if (!connection.canClose)
        {
            errorText = L"Close connection is only available for supported active TCP endpoints.";
            return false;
        }

        MIB_TCPROW row{};
        row.dwState = MIB_TCP_STATE_DELETE_TCB;
        row.dwLocalAddr = connection.tcpLocalAddrNetwork;
        row.dwRemoteAddr = connection.tcpRemoteAddrNetwork;
        row.dwLocalPort = connection.tcpLocalPortNetwork;
        row.dwRemotePort = connection.tcpRemotePortNetwork;

        const DWORD result = SetTcpEntry(&row);
        if (result != NO_ERROR)
        {
            errorText = L"Unable to close TCP connection. " + Win32ErrorToTextNetwork(result);
            return false;
        }

        return true;
    }

    bool MainWindow::HandleNetworkCommand(UINT id, UINT code)
    {
        if (id == kIdNetworkSearchEdit && code == EN_CHANGE)
        {
            wchar_t buffer[256]{};
            GetWindowTextW(networkSearchEdit_, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
            networkFilterText_ = buffer;
            ApplyNetworkFilterToList(true);
            return true;
        }

        if (id == kIdNetworkModeCombo && code == CBN_SELCHANGE)
        {
            const int selected = static_cast<int>(SendMessageW(networkModeCombo_, CB_GETCURSEL, 0, 0));
            switch (selected)
            {
            case 1:
                activeNetworkFilterMode_ = NetworkFilterMode::Tcp;
                break;
            case 2:
                activeNetworkFilterMode_ = NetworkFilterMode::Udp;
                break;
            case 3:
                activeNetworkFilterMode_ = NetworkFilterMode::Listening;
                break;
            case 4:
                activeNetworkFilterMode_ = NetworkFilterMode::Established;
                break;
            default:
                activeNetworkFilterMode_ = NetworkFilterMode::All;
                break;
            }

            ApplyNetworkFilterToList(true);
            return true;
        }

        if (id == kIdNetworkRefreshButton)
        {
            RefreshNetworkInventory(true);
            return true;
        }

        if (id == kIdNetworkTerminateButton)
        {
            const int selected = SelectedNetworkIndex();
            if (selected < 0)
            {
                MessageBoxW(hwnd_, L"Select a network endpoint first.", L"Network", MB_OK | MB_ICONINFORMATION);
                return true;
            }

            const auto &connection = networkConnections_[static_cast<size_t>(selected)];
            std::wstringstream prompt;
            prompt << L"Terminate owner process for this endpoint?\n\n"
                   << connection.protocol << L" " << connection.localEndpoint << L" -> " << connection.remoteEndpoint
                   << L"\nPID " << connection.pid << L" (" << connection.processName << L")";

            if (MessageBoxW(hwnd_, prompt.str().c_str(), L"Terminate Owner Process", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            {
                return true;
            }

            std::wstring error;
            if (!TerminateSelectedNetworkProcess(error))
            {
                MessageBoxW(hwnd_, error.c_str(), L"Terminate Owner Process", MB_OK | MB_ICONWARNING);
                return true;
            }

            RefreshNetworkInventory(true);
            return true;
        }

        if (id == kIdNetworkCloseButton)
        {
            const int selected = SelectedNetworkIndex();
            if (selected < 0)
            {
                MessageBoxW(hwnd_, L"Select a TCP endpoint first.", L"Network", MB_OK | MB_ICONINFORMATION);
                return true;
            }

            const auto &connection = networkConnections_[static_cast<size_t>(selected)];
            std::wstringstream prompt;
            prompt << L"Close this TCP connection?\n\n"
                   << connection.localEndpoint << L" -> " << connection.remoteEndpoint
                   << L"\nPID " << connection.pid << L" (" << connection.processName << L")";

            if (MessageBoxW(hwnd_, prompt.str().c_str(), L"Close TCP Connection", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
            {
                return true;
            }

            std::wstring error;
            if (!CloseSelectedNetworkConnection(error))
            {
                MessageBoxW(hwnd_, error.c_str(), L"Close TCP Connection", MB_OK | MB_ICONWARNING);
                return true;
            }

            RefreshNetworkInventory(true);
            return true;
        }

        return false;
    }

    bool MainWindow::HandleNetworkNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!hdr || hdr->hwndFrom != networkList_)
        {
            return false;
        }

        if (hdr->code == LVN_ITEMCHANGED)
        {
            const auto *change = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if (change && (change->uChanged & LVIF_STATE) != 0)
            {
                RefreshNetworkActionState();
            }
            result = 0;
            return true;
        }

        if (hdr->code == LVN_KEYDOWN)
        {
            const auto *key = reinterpret_cast<const NMLVKEYDOWN *>(lParam);
            if (key && key->wVKey == VK_F5)
            {
                RefreshNetworkInventory(true);
                result = 0;
                return true;
            }
        }

        return false;
    }

} // namespace utm::ui
