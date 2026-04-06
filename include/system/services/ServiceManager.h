#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace utm::system::services
{

    struct ServiceInfo
    {
        std::wstring serviceName;
        std::wstring displayName;
        std::wstring description = L"-";
        std::wstring startupType = L"Unknown";
        std::wstring statusText = L"Unknown";
        std::wstring accountName = L"N/A";
        std::wstring groupName = L"-";
        DWORD processId = 0;
        DWORD currentState = SERVICE_STOPPED;
        bool canStop = false;
    };

    class ServiceManager
    {
    public:
        static bool EnumerateServices(std::vector<ServiceInfo> &services, std::wstring &errorText);
        static bool StartServiceByName(const std::wstring &serviceName, std::wstring &errorText);
        static bool StopServiceByName(const std::wstring &serviceName, std::wstring &errorText);
        static bool RestartServiceByName(const std::wstring &serviceName, std::wstring &errorText);
    };

} // namespace utm::system::services
