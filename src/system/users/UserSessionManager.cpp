#include "system/users/UserSessionManager.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{
    struct SessionIdentity
    {
        DWORD sessionId = 0;
        WTS_CONNECTSTATE_CLASS connectState = WTSDown;
        std::wstring sessionName;
        std::wstring userName;
        std::wstring accountName;
        bool hasRealUser = false;
        bool isCurrentSession = false;
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

    std::wstring StateToText(WTS_CONNECTSTATE_CLASS state)
    {
        switch (state)
        {
        case WTSActive:
            return L"Active";
        case WTSConnected:
            return L"Connected";
        case WTSConnectQuery:
            return L"Connect Query";
        case WTSShadow:
            return L"Shadow";
        case WTSDisconnected:
            return L"Disconnected";
        case WTSIdle:
            return L"Idle";
        case WTSListen:
            return L"Listen";
        case WTSReset:
            return L"Reset";
        case WTSDown:
            return L"Down";
        case WTSInit:
            return L"Init";
        default:
            return L"Unknown";
        }
    }

    bool QuerySessionString(DWORD sessionId, WTS_INFO_CLASS infoClass, std::wstring &value)
    {
        value.clear();

        LPWSTR buffer = nullptr;
        DWORD bytesReturned = 0;
        if (!WTSQuerySessionInformationW(
                WTS_CURRENT_SERVER_HANDLE,
                sessionId,
                infoClass,
                &buffer,
                &bytesReturned))
        {
            return false;
        }

        if (buffer)
        {
            const size_t charCount = bytesReturned / sizeof(wchar_t);
            value.assign(buffer, charCount > 0 ? charCount - 1 : 0);
            WTSFreeMemory(buffer);
        }

        return !value.empty();
    }

    DWORD CurrentSessionId()
    {
        DWORD sessionId = 0;
        if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId))
        {
            return sessionId;
        }

        return 0;
    }

    std::wstring BuildAccountName(const std::wstring &domain, const std::wstring &user)
    {
        if (domain.empty())
        {
            return user;
        }

        if (user.empty())
        {
            return domain;
        }

        return domain + L"\\" + user;
    }

    bool IsInteractiveState(WTS_CONNECTSTATE_CLASS state)
    {
        return state == WTSActive || state == WTSConnected || state == WTSDisconnected;
    }

    bool EnumerateSessionIdentities(std::vector<SessionIdentity> &identities, std::wstring &errorText)
    {
        identities.clear();
        errorText.clear();

        const DWORD currentSessionId = CurrentSessionId();

        PWTS_SESSION_INFOW sessionList = nullptr;
        DWORD sessionCount = 0;
        if (!WTSEnumerateSessionsW(
                WTS_CURRENT_SERVER_HANDLE,
                0,
                1,
                &sessionList,
                &sessionCount))
        {
            errorText = L"WTSEnumerateSessions failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        identities.reserve(sessionCount);
        for (DWORD i = 0; i < sessionCount; ++i)
        {
            const WTS_SESSION_INFOW &raw = sessionList[i];

            SessionIdentity identity{};
            identity.sessionId = raw.SessionId;
            identity.connectState = raw.State;
            identity.sessionName = raw.pWinStationName ? raw.pWinStationName : L"-";
            identity.isCurrentSession = identity.sessionId == currentSessionId;

            std::wstring user;
            std::wstring domain;
            QuerySessionString(identity.sessionId, WTSUserName, user);
            QuerySessionString(identity.sessionId, WTSDomainName, domain);

            if (!user.empty())
            {
                identity.userName = user;
                identity.accountName = BuildAccountName(domain, user);
                identity.hasRealUser = true;
            }
            else if (identity.sessionId == 0)
            {
                identity.userName = L"Services";
                identity.accountName = L"Services";
            }
            else
            {
                identity.userName = L"Session " + std::to_wstring(identity.sessionId);
                identity.accountName = identity.userName;
            }

            identities.push_back(std::move(identity));
        }

        WTSFreeMemory(sessionList);
        return true;
    }

} // namespace

namespace utm::system::users
{

    bool UserSessionManager::EnumerateUserSessions(
        const core::model::SystemSnapshot &snapshot,
        std::vector<UserSessionInfo> &sessions,
        std::wstring &errorText)
    {
        sessions.clear();
        errorText.clear();

        std::vector<SessionIdentity> identities;
        if (!EnumerateSessionIdentities(identities, errorText))
        {
            return false;
        }

        std::unordered_map<DWORD, UserSessionInfo> bySession;
        bySession.reserve(identities.size() + 8);

        for (const auto &identity : identities)
        {
            UserSessionInfo info{};
            info.sessionId = identity.sessionId;
            info.connectState = identity.connectState;
            info.stateText = StateToText(identity.connectState);
            info.sessionName = identity.sessionName;
            info.userName = identity.userName;
            info.accountName = identity.accountName;
            info.isCurrentSession = identity.isCurrentSession;
            info.canLogoff = !identity.isCurrentSession && identity.hasRealUser;
            info.canDisconnect = !identity.isCurrentSession && identity.hasRealUser && IsInteractiveState(identity.connectState);

            bySession[info.sessionId] = std::move(info);
        }

        for (const auto &process : snapshot.processes)
        {
            DWORD sessionId = 0;
            if (!ProcessIdToSessionId(process.pid, &sessionId))
            {
                continue;
            }

            auto [it, inserted] = bySession.try_emplace(sessionId);
            UserSessionInfo &session = it->second;
            if (inserted)
            {
                session.sessionId = sessionId;
                session.connectState = WTSDown;
                session.stateText = L"Unknown";
                session.sessionName = L"-";
                session.userName = L"Session " + std::to_wstring(sessionId);
                session.accountName = session.userName;
                session.isCurrentSession = false;
            }

            ++session.processCount;
            session.cpuPercent += process.cpuPercent;
            session.memoryBytes += process.workingSetBytes;
            session.totalReadBytes += process.readBytes;
            session.totalWriteBytes += process.writeBytes;
        }

        sessions.reserve(bySession.size());
        for (auto &kv : bySession)
        {
            sessions.push_back(std::move(kv.second));
        }

        std::sort(sessions.begin(), sessions.end(), [](const UserSessionInfo &left, const UserSessionInfo &right)
                  {
                      if (left.isCurrentSession != right.isCurrentSession)
                      {
                          return left.isCurrentSession;
                      }

                      if (left.accountName == right.accountName)
                      {
                          return left.sessionId < right.sessionId;
                      }

                      return left.accountName < right.accountName;
                  });

        return true;
    }

    bool UserSessionManager::EnumerateUserProcesses(
        const core::model::SystemSnapshot &snapshot,
        std::vector<UserProcessInfo> &processes,
        std::wstring &errorText)
    {
        processes.clear();
        errorText.clear();

        std::vector<SessionIdentity> identities;
        if (!EnumerateSessionIdentities(identities, errorText))
        {
            return false;
        }

        std::unordered_map<DWORD, std::wstring> accountBySession;
        accountBySession.reserve(identities.size() + 8);
        for (const auto &identity : identities)
        {
            accountBySession[identity.sessionId] = identity.accountName;
        }

        processes.reserve(snapshot.processes.size());
        for (const auto &process : snapshot.processes)
        {
            DWORD sessionId = 0;
            if (!ProcessIdToSessionId(process.pid, &sessionId))
            {
                continue;
            }

            UserProcessInfo info{};
            info.pid = process.pid;
            info.sessionId = sessionId;
            info.imageName = process.imageName;
            info.cpuPercent = process.cpuPercent;
            info.memoryBytes = process.workingSetBytes;
            info.readBytes = process.readBytes;
            info.writeBytes = process.writeBytes;

            const auto account = accountBySession.find(sessionId);
            info.accountName = account != accountBySession.end() ? account->second : (L"Session " + std::to_wstring(sessionId));

            processes.push_back(std::move(info));
        }

        std::sort(processes.begin(), processes.end(), [](const UserProcessInfo &left, const UserProcessInfo &right)
                  {
                      if (left.accountName == right.accountName)
                      {
                          if (left.imageName == right.imageName)
                          {
                              return left.pid < right.pid;
                          }

                          return left.imageName < right.imageName;
                      }

                      return left.accountName < right.accountName;
                  });

        return true;
    }

    bool UserSessionManager::LogoffSession(DWORD sessionId, std::wstring &errorText)
    {
        errorText.clear();

        if (!WTSLogoffSession(WTS_CURRENT_SERVER_HANDLE, sessionId, FALSE))
        {
            errorText = L"WTSLogoffSession failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        return true;
    }

    bool UserSessionManager::DisconnectSession(DWORD sessionId, std::wstring &errorText)
    {
        errorText.clear();

        if (!WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, sessionId, FALSE))
        {
            errorText = L"WTSDisconnectSession failed. LastError=" + Win32ErrorToText(GetLastError());
            return false;
        }

        return true;
    }

} // namespace utm::system::users
