#include "ui/HardwarePanel.h"

#include "ui/MainWindow.h"

#include <cfgmgr32.h>
#include <commctrl.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <vector>

namespace
{
    constexpr UINT kIdHardwareList = 1051;
    constexpr UINT kIdHardwareStatus = 1052;
    constexpr UINT kIdHardwareRefreshButton = 1053;
    constexpr UINT kIdHardwareToggleButton = 1054;
    constexpr UINT kIdHardwareSearchEdit = 1056;

    std::wstring ToLowerHardware(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    bool ContainsInsensitiveHardware(const std::wstring &text, const std::wstring &loweredQuery)
    {
        if (loweredQuery.empty())
        {
            return true;
        }

        const std::wstring loweredText = ToLowerHardware(text);
        return loweredText.find(loweredQuery) != std::wstring::npos;
    }

    bool IsProtectedHardwareClass(const std::wstring &className)
    {
        const std::wstring lowered = ToLowerHardware(className);
        return lowered == L"system" ||
               lowered == L"processor" ||
               lowered == L"computer" ||
               lowered == L"firmware" ||
               lowered == L"acpi" ||
               lowered == L"securitydevices" ||
               lowered == L"battery" ||
               lowered == L"memory";
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

    std::wstring ConfigRetToText(CONFIGRET result)
    {
        const DWORD win32 = CM_MapCrToWin32Err(result, ERROR_GEN_FAILURE);
        std::wstringstream out;
        out << L"ConfigManager=" << result << L", Win32=" << Win32ErrorToText(win32);
        return out.str();
    }

    std::wstring DeviceRegistryString(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA &devInfoData, DWORD property)
    {
        wchar_t buffer[1024]{};
        DWORD type = 0;
        if (!SetupDiGetDeviceRegistryPropertyW(
                deviceInfoSet,
                &devInfoData,
                property,
                &type,
                reinterpret_cast<PBYTE>(buffer),
                static_cast<DWORD>(sizeof(buffer)),
                nullptr))
        {
            return L"";
        }

        if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
        {
            return L"";
        }

        return buffer;
    }

    std::wstring DevicePropertyString(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA &devInfoData, const DEVPROPKEY &property)
    {
        DEVPROPTYPE type = 0;
        DWORD size = 0;
        if (!SetupDiGetDevicePropertyW(deviceInfoSet, &devInfoData, &property, &type, nullptr, 0, &size, 0))
        {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                return L"";
            }
        }

        if (size < sizeof(wchar_t))
        {
            return L"";
        }

        std::vector<BYTE> buffer(size);
        if (!SetupDiGetDevicePropertyW(
                deviceInfoSet,
                &devInfoData,
                &property,
                &type,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &size,
                0))
        {
            return L"";
        }

        const DEVPROPTYPE baseType = type & DEVPROP_MASK_TYPE;
        if (baseType != DEVPROP_TYPE_STRING && baseType != DEVPROP_TYPE_STRING_LIST)
        {
            return L"";
        }

        const wchar_t *text = reinterpret_cast<const wchar_t *>(buffer.data());
        if (!text || text[0] == L'\0')
        {
            return L"";
        }

        return std::wstring(text);
    }

    std::wstring DeviceStatusLabel(ULONG status, ULONG problemCode)
    {
        const bool disabled = problemCode == CM_PROB_DISABLED || problemCode == CM_PROB_HARDWARE_DISABLED;
        if (disabled)
        {
            return L"Disabled";
        }

        if ((status & DN_STARTED) != 0)
        {
            return L"Running";
        }

        if ((status & DN_HAS_PROBLEM) != 0)
        {
            return L"Problem " + std::to_wstring(problemCode);
        }

        return L"Stopped";
    }

} // namespace

namespace utm::ui
{

    void HardwarePanel::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool HardwarePanel::HandleCommand(UINT id, UINT code)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleHardwareCommand(id, code);
    }

    bool HardwarePanel::HandleNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!owner_)
        {
            return false;
        }

        return owner_->HandleHardwareNotify(hdr, lParam, result);
    }

    int MainWindow::SelectedHardwareIndex() const
    {
        if (!hardwareList_)
        {
            return -1;
        }

        const int selectedRow = ListView_GetNextItem(hardwareList_, -1, LVNI_SELECTED);
        if (selectedRow < 0 || static_cast<size_t>(selectedRow) >= hardwareVisibleRows_.size())
        {
            return -1;
        }

        const size_t hardwareIndex = hardwareVisibleRows_[static_cast<size_t>(selectedRow)];
        if (hardwareIndex >= hardwareDevices_.size())
        {
            return -1;
        }

        return static_cast<int>(hardwareIndex);
    }

    void MainWindow::RefreshHardwareActionState()
    {
        if (!hardwareToggleButton_)
        {
            return;
        }

        const int selected = SelectedHardwareIndex();
        if (selected < 0)
        {
            SetWindowTextW(hardwareToggleButton_, L"Disable Selected");
            EnableWindow(hardwareToggleButton_, FALSE);

            if (hardwareStatus_)
            {
                std::wstringstream summary;
                summary << L"Showing " << hardwareVisibleRows_.size() << L" of " << hardwareDevices_.size() << L" detected devices.";
                if (!hardwareFilterText_.empty())
                {
                    summary << L" Filter: " << hardwareFilterText_;
                }
                SetWindowTextW(hardwareStatus_, summary.str().c_str());
            }
            return;
        }

        const auto &device = hardwareDevices_[static_cast<size_t>(selected)];
        const bool willEnable = !device.enabled;
        const bool protectedClass = IsProtectedHardwareClass(device.className);
        const bool canDisable = device.disableCapable && !protectedClass;
        const bool canChange = willEnable || canDisable;

        SetWindowTextW(hardwareToggleButton_, willEnable ? L"Enable Selected" : L"Disable Selected");
        EnableWindow(hardwareToggleButton_, canChange ? TRUE : FALSE);

        if (hardwareStatus_)
        {
            std::wstringstream detail;
            detail << L"Selected: " << device.name
                   << L" | Class: " << device.className
                   << L" | State: " << device.statusText
                   << L" | Visible " << hardwareVisibleRows_.size() << L"/" << hardwareDevices_.size();

            if (!willEnable)
            {
                if (protectedClass)
                {
                    detail << L" | Protected critical class";
                }
                else if (!device.disableCapable)
                {
                    detail << L" | Device does not expose disable capability";
                }
            }

            SetWindowTextW(hardwareStatus_, detail.str().c_str());
        }
    }

    void MainWindow::ApplyHardwareFilterToList(bool preserveSelection)
    {
        if (!hardwareList_)
        {
            return;
        }

        std::wstring selectedInstanceId;
        if (preserveSelection)
        {
            const int selected = SelectedHardwareIndex();
            if (selected >= 0)
            {
                selectedInstanceId = hardwareDevices_[static_cast<size_t>(selected)].instanceId;
            }
        }

        const int previousTopIndex = ListView_GetTopIndex(hardwareList_);
        const std::wstring loweredQuery = ToLowerHardware(hardwareFilterText_);

        SendMessageW(hardwareList_, WM_SETREDRAW, FALSE, 0);

        ListView_DeleteAllItems(hardwareList_);
        hardwareVisibleRows_.clear();
        hardwareVisibleRows_.reserve(hardwareDevices_.size());

        int selectedRow = -1;

        for (size_t i = 0; i < hardwareDevices_.size(); ++i)
        {
            const auto &device = hardwareDevices_[i];
            const bool pass = loweredQuery.empty() ||
                              ContainsInsensitiveHardware(device.name, loweredQuery) ||
                              ContainsInsensitiveHardware(device.className, loweredQuery) ||
                              ContainsInsensitiveHardware(device.location, loweredQuery) ||
                              ContainsInsensitiveHardware(device.statusText, loweredQuery) ||
                              ContainsInsensitiveHardware(device.instanceId, loweredQuery);

            if (!pass)
            {
                continue;
            }

            const int row = static_cast<int>(hardwareVisibleRows_.size());
            hardwareVisibleRows_.push_back(i);

            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = row;
            item.pszText = const_cast<LPWSTR>(device.name.c_str());

            const int inserted = ListView_InsertItem(hardwareList_, &item);
            if (inserted < 0)
            {
                continue;
            }

            ListView_SetItemText(hardwareList_, inserted, 1, const_cast<LPWSTR>(device.className.c_str()));
            ListView_SetItemText(hardwareList_, inserted, 2, const_cast<LPWSTR>(device.statusText.c_str()));
            ListView_SetItemText(hardwareList_, inserted, 3, const_cast<LPWSTR>(device.location.c_str()));

            if (!selectedInstanceId.empty() && device.instanceId == selectedInstanceId)
            {
                selectedRow = inserted;
            }
        }

        if (selectedRow < 0 && !hardwareVisibleRows_.empty())
        {
            selectedRow = 0;
        }

        if (selectedRow >= 0)
        {
            ListView_SetItemState(hardwareList_, selectedRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(hardwareList_, selectedRow, FALSE);
        }

        const int visibleCount = ListView_GetItemCount(hardwareList_);
        if (visibleCount > 0 && previousTopIndex > 0)
        {
            const int targetTop = (std::min)(previousTopIndex, visibleCount - 1);
            ListView_EnsureVisible(hardwareList_, targetTop, FALSE);
        }

        SendMessageW(hardwareList_, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hardwareList_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);

        RefreshHardwareActionState();
    }

    void MainWindow::RefreshHardwareInventory(bool preserveSelection)
    {
        if (!hardwareList_)
        {
            return;
        }

        std::vector<HardwareDeviceEntry> refreshed;

        HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (deviceInfoSet == INVALID_HANDLE_VALUE)
        {
            if (hardwareStatus_)
            {
                const std::wstring text = L"Failed to enumerate devices. " + Win32ErrorToText(GetLastError());
                SetWindowTextW(hardwareStatus_, text.c_str());
            }
            RefreshHardwareActionState();
            return;
        }

        for (DWORD index = 0;; ++index)
        {
            SP_DEVINFO_DATA devInfoData{};
            devInfoData.cbSize = sizeof(devInfoData);

            if (!SetupDiEnumDeviceInfo(deviceInfoSet, index, &devInfoData))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                {
                    break;
                }
                continue;
            }

            HardwareDeviceEntry entry{};

            wchar_t instanceIdBuffer[512]{};
            if (SetupDiGetDeviceInstanceIdW(
                    deviceInfoSet,
                    &devInfoData,
                    instanceIdBuffer,
                    static_cast<DWORD>(std::size(instanceIdBuffer)),
                    nullptr))
            {
                entry.instanceId = instanceIdBuffer;
            }
            else
            {
                entry.instanceId = L"UNKNOWN\\" + std::to_wstring(index);
            }

            entry.name = DevicePropertyString(deviceInfoSet, devInfoData, DEVPKEY_Device_FriendlyName);
            if (entry.name.empty())
            {
                entry.name = DevicePropertyString(deviceInfoSet, devInfoData, DEVPKEY_Device_DeviceDesc);
            }
            if (entry.name.empty())
            {
                entry.name = DeviceRegistryString(deviceInfoSet, devInfoData, SPDRP_FRIENDLYNAME);
            }
            if (entry.name.empty())
            {
                entry.name = DeviceRegistryString(deviceInfoSet, devInfoData, SPDRP_DEVICEDESC);
            }
            if (entry.name.empty())
            {
                entry.name = L"(Unnamed Device)";
            }

            entry.className = DevicePropertyString(deviceInfoSet, devInfoData, DEVPKEY_Device_Class);
            if (entry.className.empty())
            {
                entry.className = DeviceRegistryString(deviceInfoSet, devInfoData, SPDRP_CLASS);
            }
            if (entry.className.empty())
            {
                entry.className = L"Unknown";
            }

            entry.location = DevicePropertyString(deviceInfoSet, devInfoData, DEVPKEY_Device_LocationInfo);
            if (entry.location.empty())
            {
                entry.location = DeviceRegistryString(deviceInfoSet, devInfoData, SPDRP_LOCATION_INFORMATION);
            }
            if (entry.location.empty())
            {
                entry.location = L"N/A";
            }

            ULONG status = 0;
            ULONG problemCode = 0;
            const CONFIGRET statusResult = CM_Get_DevNode_Status(&status, &problemCode, devInfoData.DevInst, 0);
            if (statusResult == CR_SUCCESS)
            {
                const bool disabled = problemCode == CM_PROB_DISABLED || problemCode == CM_PROB_HARDWARE_DISABLED;
                entry.enabled = ((status & DN_STARTED) != 0) && !disabled;
                entry.statusText = DeviceStatusLabel(status, problemCode);
                entry.disableCapable = (status & DN_DISABLEABLE) != 0;
            }
            else
            {
                entry.enabled = false;
                entry.statusText = L"Unknown";
                entry.disableCapable = true;
            }

            if (IsProtectedHardwareClass(entry.className))
            {
                entry.disableCapable = false;
            }

            refreshed.push_back(std::move(entry));
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);

        std::sort(refreshed.begin(), refreshed.end(), [](const HardwareDeviceEntry &left, const HardwareDeviceEntry &right)
                  {
                      const std::wstring leftClass = ToLowerHardware(left.className);
                      const std::wstring rightClass = ToLowerHardware(right.className);
                      if (leftClass != rightClass)
                      {
                          return leftClass < rightClass;
                      }

                      const std::wstring leftName = ToLowerHardware(left.name);
                      const std::wstring rightName = ToLowerHardware(right.name);
                      if (leftName != rightName)
                      {
                          return leftName < rightName;
                      }

                      return left.instanceId < right.instanceId; });

        hardwareDevices_ = std::move(refreshed);
        lastHardwareRefreshTickMs_ = GetTickCount64();

        ApplyHardwareFilterToList(preserveSelection);
    }

    bool MainWindow::SetHardwareDeviceEnabled(size_t index, bool enable, std::wstring &errorText)
    {
        errorText.clear();
        if (index >= hardwareDevices_.size())
        {
            errorText = L"No hardware device is selected.";
            return false;
        }

        const auto &device = hardwareDevices_[index];
        if (device.instanceId.empty())
        {
            errorText = L"The selected device does not expose a valid instance ID.";
            return false;
        }

        if (!enable && IsProtectedHardwareClass(device.className))
        {
            errorText = L"Disabling this protected device class is blocked to prevent system instability.";
            return false;
        }

        if (!enable && !device.disableCapable)
        {
            errorText = L"The selected device does not report disable capability.";
            return false;
        }

        HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
        if (deviceInfoSet == INVALID_HANDLE_VALUE)
        {
            errorText = L"Unable to open the device information set. " + Win32ErrorToText(GetLastError());
            return false;
        }

        SP_DEVINFO_DATA devInfoData{};
        devInfoData.cbSize = sizeof(devInfoData);
        if (!SetupDiOpenDeviceInfoW(deviceInfoSet, device.instanceId.c_str(), nullptr, 0, &devInfoData))
        {
            errorText = L"Unable to open selected device. " + Win32ErrorToText(GetLastError());
            SetupDiDestroyDeviceInfoList(deviceInfoSet);
            return false;
        }

        CONFIGRET result = CR_CALL_NOT_IMPLEMENTED;
        if (enable)
        {
            result = CM_Enable_DevNode(devInfoData.DevInst, 0);
        }
        else
        {
            result = CM_Disable_DevNode(devInfoData.DevInst, 0);
            if (result != CR_SUCCESS)
            {
                result = CM_Disable_DevNode(devInfoData.DevInst, CM_DISABLE_PERSIST);
            }
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);

        if (result != CR_SUCCESS)
        {
            errorText = L"Device state change failed. " + ConfigRetToText(result);
            return false;
        }

        return true;
    }

    bool MainWindow::HandleHardwareCommand(UINT id, UINT code)
    {
        if (id == kIdHardwareSearchEdit && code == EN_CHANGE)
        {
            wchar_t buffer[256]{};
            GetWindowTextW(hardwareSearchEdit_, buffer, static_cast<int>(std::size(buffer)));
            hardwareFilterText_ = buffer;
            ApplyHardwareFilterToList(true);
            return true;
        }

        if (id == kIdHardwareRefreshButton)
        {
            RefreshHardwareInventory(true);
            return true;
        }

        if (id != kIdHardwareToggleButton)
        {
            return false;
        }

        const int selected = SelectedHardwareIndex();
        if (selected < 0)
        {
            MessageBoxW(hwnd_, L"Select a hardware device first.", L"Hardware", MB_OK | MB_ICONINFORMATION);
            return true;
        }

        const auto &device = hardwareDevices_[static_cast<size_t>(selected)];
        const bool enable = !device.enabled;
        const std::wstring action = enable ? L"enable" : L"disable";

        std::wstring confirm = L"Do you want to ";
        confirm += action;
        confirm += L" this device?\n\n";
        confirm += device.name;
        confirm += L"\nClass: ";
        confirm += device.className;
        confirm += L"\n\nThis may disconnect the device immediately.";

        if (MessageBoxW(hwnd_, confirm.c_str(), L"Hardware Device Action", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        {
            return true;
        }

        std::wstring error;
        if (!SetHardwareDeviceEnabled(static_cast<size_t>(selected), enable, error))
        {
            if (error.empty())
            {
                error = L"Unable to change device state.";
            }
            MessageBoxW(hwnd_, error.c_str(), L"Hardware Device Action", MB_OK | MB_ICONWARNING);
            return true;
        }

        RefreshHardwareInventory(true);
        return true;
    }

    bool MainWindow::HandleHardwareNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result)
    {
        if (!hdr || hdr->hwndFrom != hardwareList_)
        {
            return false;
        }

        if (hdr->code == LVN_ITEMCHANGED)
        {
            const auto *change = reinterpret_cast<const NMLISTVIEW *>(lParam);
            if (change && (change->uChanged & LVIF_STATE) != 0)
            {
                RefreshHardwareActionState();
            }
            result = 0;
            return true;
        }

        if (hdr->code == NM_DBLCLK)
        {
            HandleHardwareCommand(kIdHardwareToggleButton, BN_CLICKED);
            result = 0;
            return true;
        }

        if (hdr->code == LVN_KEYDOWN)
        {
            const auto *key = reinterpret_cast<const NMLVKEYDOWN *>(lParam);
            if (key && key->wVKey == VK_SPACE)
            {
                HandleHardwareCommand(kIdHardwareToggleButton, BN_CLICKED);
                result = 0;
                return true;
            }
        }

        return false;
    }

} // namespace utm::ui
