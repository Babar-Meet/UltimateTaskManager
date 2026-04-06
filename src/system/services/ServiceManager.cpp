#include "system/services/ServiceManager.h"

#include <winsvc.h>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <sstream>
#include <vector>

namespace
{
    class ScopedServiceHandle
    {
    public:
        ScopedServiceHandle() = default;
        explicit ScopedServiceHandle(SC_HANDLE handle) : handle_(handle)
        {
        }

        ScopedServiceHandle(const ScopedServiceHandle &) = delete;
        ScopedServiceHandle &operator=(const ScopedServiceHandle &) = delete;

        ScopedServiceHandle(ScopedServiceHandle &&other) noexcept : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        ScopedServiceHandle &operator=(ScopedServiceHandle &&other) noexcept
        {
            if (this != &other)
            {
                Reset();
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        ~ScopedServiceHandle()
        {
            Reset();
        }

        void Reset(SC_HANDLE handle = nullptr)
        {
            if (handle_)
            {
                CloseServiceHandle(handle_);
            }
            handle_ = handle;
        }

        SC_HANDLE Get() const
        {
            return handle_;
        }

    private:
        SC_HANDLE handle_ = nullptr;
    };

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

    std::wstring ToLower(const std::wstring &value)
    {
        std::wstring lowered = value;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    std::wstring ServiceStateToText(DWORD state)
    {
        switch (state)
        {
        case SERVICE_STOPPED:
            return L"Stopped";
        case SERVICE_START_PENDING:
            return L"Starting";
        case SERVICE_STOP_PENDING:
            return L"Stopping";
        case SERVICE_RUNNING:
            return L"Running";
        case SERVICE_CONTINUE_PENDING:
            return L"Continuing";
        case SERVICE_PAUSE_PENDING:
            return L"Pausing";
        case SERVICE_PAUSED:
            return L"Paused";
        default:
            return L"Unknown";
        }
    }

    std::wstring StartupTypeToText(DWORD startType, bool delayedAutoStart)
    {
        switch (startType)
        {
        case SERVICE_AUTO_START:
            return delayedAutoStart ? L"Automatic (Delayed)" : L"Automatic";
        case SERVICE_DEMAND_START:
            return L"Manual";
        case SERVICE_DISABLED:
            return L"Disabled";
        case SERVICE_BOOT_START:
            return L"Boot";
        case SERVICE_SYSTEM_START:
            return L"System";
        default:
            return L"Unknown";
        }
    }

    bool QueryServiceStatus(SC_HANDLE service, SERVICE_STATUS_PROCESS &status, std::wstring &errorText)
    {
        DWORD bytesNeeded = 0;
        if (!QueryServiceStatusEx(
                service,
                SC_STATUS_PROCESS_INFO,
                reinterpret_cast<LPBYTE>(&status),
                sizeof(status),
                &bytesNeeded))
        {
            errorText = L"QueryServiceStatusEx failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        return true;
    }

    bool WaitForState(SC_HANDLE service, DWORD targetState, DWORD timeoutMs, std::wstring &errorText)
    {
        const std::uint64_t deadline = GetTickCount64() + timeoutMs;

        while (true)
        {
            SERVICE_STATUS_PROCESS status{};
            if (!QueryServiceStatus(service, status, errorText))
            {
                return false;
            }

            if (status.dwCurrentState == targetState)
            {
                return true;
            }

            if (GetTickCount64() >= deadline)
            {
                std::wstringstream out;
                out << L"Timed out waiting for service state transition. Expected="
                    << ServiceStateToText(targetState)
                    << L", Current="
                    << ServiceStateToText(status.dwCurrentState);
                errorText = out.str();
                return false;
            }

            DWORD waitMs = status.dwWaitHint;
            if (waitMs < 150)
            {
                waitMs = 150;
            }
            else if (waitMs > 1000)
            {
                waitMs = 1000;
            }

            Sleep(waitMs);
        }
    }

    void FillServiceConfigDetails(SC_HANDLE scmHandle, utm::system::services::ServiceInfo &info)
    {
        ScopedServiceHandle service(OpenServiceW(scmHandle, info.serviceName.c_str(), SERVICE_QUERY_CONFIG));
        if (!service.Get())
        {
            return;
        }

        DWORD cfgSize = 0;
        QueryServiceConfigW(service.Get(), nullptr, 0, &cfgSize);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && cfgSize > 0)
        {
            std::vector<BYTE> cfgBuffer(cfgSize);
            auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(cfgBuffer.data());
            if (QueryServiceConfigW(service.Get(), config, static_cast<DWORD>(cfgBuffer.size()), &cfgSize))
            {
                bool delayedAutoStart = false;
                SERVICE_DELAYED_AUTO_START_INFO delayedInfo{};
                DWORD delayedSize = 0;
                if (QueryServiceConfig2W(
                        service.Get(),
                        SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                        reinterpret_cast<LPBYTE>(&delayedInfo),
                        sizeof(delayedInfo),
                        &delayedSize))
                {
                    delayedAutoStart = delayedInfo.fDelayedAutostart != FALSE;
                }

                info.startupType = StartupTypeToText(config->dwStartType, delayedAutoStart);

                if (config->lpServiceStartName && config->lpServiceStartName[0] != L'\0')
                {
                    info.accountName = config->lpServiceStartName;
                }

                if (config->lpLoadOrderGroup && config->lpLoadOrderGroup[0] != L'\0')
                {
                    info.groupName = config->lpLoadOrderGroup;
                }
            }
        }

        DWORD descSize = 0;
        QueryServiceConfig2W(service.Get(), SERVICE_CONFIG_DESCRIPTION, nullptr, 0, &descSize);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && descSize >= sizeof(SERVICE_DESCRIPTIONW))
        {
            std::vector<BYTE> descBuffer(descSize);
            if (QueryServiceConfig2W(
                    service.Get(),
                    SERVICE_CONFIG_DESCRIPTION,
                    descBuffer.data(),
                    static_cast<DWORD>(descBuffer.size()),
                    &descSize))
            {
                const auto *description = reinterpret_cast<const SERVICE_DESCRIPTIONW *>(descBuffer.data());
                if (description->lpDescription && description->lpDescription[0] != L'\0')
                {
                    info.description = description->lpDescription;
                }
            }
        }
    }

} // namespace

namespace utm::system::services
{

    bool ServiceManager::EnumerateServices(std::vector<ServiceInfo> &services, std::wstring &errorText)
    {
        errorText.clear();

        ScopedServiceHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE));
        if (!scm.Get())
        {
            errorText = L"OpenSCManager failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        services.clear();
        std::vector<BYTE> buffer(64 * 1024);
        DWORD resumeHandle = 0;

        while (true)
        {
            DWORD bytesNeeded = 0;
            DWORD returnedCount = 0;
            const BOOL ok = EnumServicesStatusExW(
                scm.Get(),
                SC_ENUM_PROCESS_INFO,
                SERVICE_WIN32,
                SERVICE_STATE_ALL,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytesNeeded,
                &returnedCount,
                &resumeHandle,
                nullptr);

            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            if (!ok && error != ERROR_MORE_DATA)
            {
                errorText = L"EnumServicesStatusExW failed. LastError=" + Win32ErrorToText(error);
                return false;
            }

            const auto *entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW *>(buffer.data());
            for (DWORD i = 0; i < returnedCount; ++i)
            {
                ServiceInfo info{};
                info.serviceName = entries[i].lpServiceName ? entries[i].lpServiceName : L"";
                info.displayName = entries[i].lpDisplayName ? entries[i].lpDisplayName : info.serviceName;
                info.currentState = entries[i].ServiceStatusProcess.dwCurrentState;
                info.statusText = ServiceStateToText(info.currentState);
                info.processId = entries[i].ServiceStatusProcess.dwProcessId;
                info.canStop = (entries[i].ServiceStatusProcess.dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0;

                FillServiceConfigDetails(scm.Get(), info);
                services.push_back(std::move(info));
            }

            if (ok)
            {
                break;
            }

            if (bytesNeeded > buffer.size())
            {
                buffer.resize(bytesNeeded);
            }
        }

        std::sort(services.begin(), services.end(), [](const ServiceInfo &left, const ServiceInfo &right)
                  {
                      const std::wstring leftName = ToLower(left.displayName);
                      const std::wstring rightName = ToLower(right.displayName);

                      if (leftName == rightName)
                      {
                          return ToLower(left.serviceName) < ToLower(right.serviceName);
                      }

                      return leftName < rightName;
                  });

        return true;
    }

    bool ServiceManager::StartServiceByName(const std::wstring &serviceName, std::wstring &errorText)
    {
        errorText.clear();

        ScopedServiceHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!scm.Get())
        {
            errorText = L"OpenSCManager failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        ScopedServiceHandle service(OpenServiceW(
            scm.Get(),
            serviceName.c_str(),
            SERVICE_QUERY_STATUS | SERVICE_START));
        if (!service.Get())
        {
            errorText = L"OpenService failed for start action. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        SERVICE_STATUS_PROCESS status{};
        if (!QueryServiceStatus(service.Get(), status, errorText))
        {
            return false;
        }

        if (status.dwCurrentState == SERVICE_RUNNING)
        {
            return true;
        }

        if (status.dwCurrentState == SERVICE_STOP_PENDING)
        {
            if (!WaitForState(service.Get(), SERVICE_STOPPED, 15000, errorText))
            {
                return false;
            }
        }

        if (!StartServiceW(service.Get(), 0, nullptr))
        {
            const DWORD error = GetLastError();
            if (error != ERROR_SERVICE_ALREADY_RUNNING)
            {
                errorText = L"StartService failed. LastError=" + Win32ErrorToText(error);
                return false;
            }
        }

        return WaitForState(service.Get(), SERVICE_RUNNING, 20000, errorText);
    }

    bool ServiceManager::StopServiceByName(const std::wstring &serviceName, std::wstring &errorText)
    {
        errorText.clear();

        ScopedServiceHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!scm.Get())
        {
            errorText = L"OpenSCManager failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        ScopedServiceHandle service(OpenServiceW(
            scm.Get(),
            serviceName.c_str(),
            SERVICE_QUERY_STATUS | SERVICE_STOP));
        if (!service.Get())
        {
            errorText = L"OpenService failed for stop action. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        SERVICE_STATUS_PROCESS status{};
        if (!QueryServiceStatus(service.Get(), status, errorText))
        {
            return false;
        }

        if (status.dwCurrentState == SERVICE_STOPPED)
        {
            return true;
        }

        if (status.dwCurrentState == SERVICE_STOP_PENDING)
        {
            return WaitForState(service.Get(), SERVICE_STOPPED, 20000, errorText);
        }

        SERVICE_STATUS serviceStatus{};
        if (!ControlService(service.Get(), SERVICE_CONTROL_STOP, &serviceStatus))
        {
            const DWORD error = GetLastError();
            if (error != ERROR_SERVICE_NOT_ACTIVE)
            {
                errorText = L"ControlService(SERVICE_CONTROL_STOP) failed. LastError=" + Win32ErrorToText(error);
                return false;
            }
        }

        return WaitForState(service.Get(), SERVICE_STOPPED, 20000, errorText);
    }

    bool ServiceManager::RestartServiceByName(const std::wstring &serviceName, std::wstring &errorText)
    {
        errorText.clear();

        ScopedServiceHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!scm.Get())
        {
            errorText = L"OpenSCManager failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        ScopedServiceHandle service(OpenServiceW(
            scm.Get(),
            serviceName.c_str(),
            SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START));
        if (!service.Get())
        {
            errorText = L"OpenService failed for restart action. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        SERVICE_STATUS_PROCESS status{};
        if (!QueryServiceStatus(service.Get(), status, errorText))
        {
            return false;
        }

        if (status.dwCurrentState != SERVICE_STOPPED)
        {
            if (status.dwCurrentState == SERVICE_STOP_PENDING)
            {
                if (!WaitForState(service.Get(), SERVICE_STOPPED, 20000, errorText))
                {
                    return false;
                }
            }
            else
            {
                SERVICE_STATUS serviceStatus{};
                if (!ControlService(service.Get(), SERVICE_CONTROL_STOP, &serviceStatus))
                {
                    const DWORD stopError = GetLastError();
                    if (stopError != ERROR_SERVICE_NOT_ACTIVE)
                    {
                        errorText = L"ControlService(SERVICE_CONTROL_STOP) failed. LastError=" + Win32ErrorToText(stopError);
                        return false;
                    }
                }

                if (!WaitForState(service.Get(), SERVICE_STOPPED, 20000, errorText))
                {
                    return false;
                }
            }
        }

        if (!StartServiceW(service.Get(), 0, nullptr))
        {
            const DWORD startError = GetLastError();
            if (startError != ERROR_SERVICE_ALREADY_RUNNING)
            {
                errorText = L"StartService failed. LastError=" + Win32ErrorToText(startError);
                return false;
            }
        }

        return WaitForState(service.Get(), SERVICE_RUNNING, 20000, errorText);
    }

} // namespace utm::system::services
