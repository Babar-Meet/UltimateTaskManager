#pragma once

#include "core/model/ProcessSnapshot.h"

#include <windows.h>
#include <wtsapi32.h>

#include <cstdint>
#include <string>
#include <vector>

namespace utm::system::users
{

    struct UserSessionInfo
    {
        DWORD sessionId = 0;
        WTS_CONNECTSTATE_CLASS connectState = WTSDown;

        std::wstring sessionName;
        std::wstring userName;
        std::wstring accountName;
        std::wstring stateText;

        bool isCurrentSession = false;
        bool canLogoff = false;
        bool canDisconnect = false;

        std::uint32_t processCount = 0;
        double cpuPercent = 0.0;
        std::uint64_t memoryBytes = 0;
        std::uint64_t totalReadBytes = 0;
        std::uint64_t totalWriteBytes = 0;
        double diskMBps = 0.0;
        double networkMbps = 0.0;
    };

    struct UserProcessInfo
    {
        std::uint32_t pid = 0;
        DWORD sessionId = 0;
        std::wstring accountName;
        std::wstring imageName;

        double cpuPercent = 0.0;
        std::uint64_t memoryBytes = 0;
        std::uint64_t readBytes = 0;
        std::uint64_t writeBytes = 0;
        double diskMBps = 0.0;
    };

    class UserSessionManager
    {
    public:
        static bool EnumerateUserSessions(
            const core::model::SystemSnapshot &snapshot,
            std::vector<UserSessionInfo> &sessions,
            std::wstring &errorText);

        static bool EnumerateUserProcesses(
            const core::model::SystemSnapshot &snapshot,
            std::vector<UserProcessInfo> &processes,
            std::wstring &errorText);

        static bool LogoffSession(DWORD sessionId, std::wstring &errorText);
        static bool DisconnectSession(DWORD sessionId, std::wstring &errorText);
    };

} // namespace utm::system::users
