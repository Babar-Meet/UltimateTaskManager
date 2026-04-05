#include "tools/process/ProcessActions.h"

#include "system/ntapi/NtApi.h"

#include <RestartManager.h>
#include <filesystem>
#include <tlhelp32.h>
#include <shellapi.h>
#include <set>
#include <vector>
#include <unordered_map>

namespace
{

    struct CloseWindowsContext
    {
        DWORD pid = 0;
        std::vector<HWND> windows;
    };

    BOOL CALLBACK CollectWindowsByPid(HWND hwnd, LPARAM lParam)
    {
        auto *context = reinterpret_cast<CloseWindowsContext *>(lParam);
        DWORD ownerPid = 0;
        GetWindowThreadProcessId(hwnd, &ownerPid);

        if (ownerPid == context->pid && IsWindowVisible(hwnd))
        {
            context->windows.push_back(hwnd);
        }

        return TRUE;
    }

    bool OpenTargetProcess(DWORD pid, DWORD access, HANDLE &out, std::wstring &error)
    {
        out = OpenProcess(access, FALSE, pid);
        if (!out)
        {
            error = L"OpenProcess failed.";
            return false;
        }

        return true;
    }

    std::wstring Widen(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    bool TryDeletePathNow(const std::filesystem::path &path, bool isDirectory, std::wstring &error)
    {
        std::error_code ec;

        const bool exists = std::filesystem::exists(path, ec);
        if (ec)
        {
            error = L"Failed to access path: " + Widen(ec.message());
            return false;
        }

        if (!exists)
        {
            return true;
        }

        if (isDirectory)
        {
            std::filesystem::remove_all(path, ec);
            if (!ec && !std::filesystem::exists(path))
            {
                return true;
            }
        }
        else
        {
            (void)std::filesystem::remove(path, ec);
            if (!ec && !std::filesystem::exists(path))
            {
                return true;
            }
        }

        if (ec)
        {
            error = L"Delete failed: " + Widen(ec.message());
        }
        else
        {
            error = L"Delete failed because the path is likely in use by another process.";
        }

        return false;
    }

    bool GetLockingPids(const std::wstring &path, std::vector<DWORD> &pids, std::wstring &error)
    {
        pids.clear();

        DWORD sessionHandle = 0;
        WCHAR sessionKey[CCH_RM_SESSION_KEY + 1]{};
        DWORD status = RmStartSession(&sessionHandle, 0, sessionKey);
        if (status != ERROR_SUCCESS)
        {
            error = L"RmStartSession failed (" + std::to_wstring(status) + L")";
            return false;
        }

        LPCWSTR resources[1] = {path.c_str()};
        status = RmRegisterResources(sessionHandle, 1, resources, 0, nullptr, 0, nullptr);
        if (status != ERROR_SUCCESS)
        {
            RmEndSession(sessionHandle);
            error = L"RmRegisterResources failed (" + std::to_wstring(status) + L")";
            return false;
        }

        UINT needed = 0;
        UINT count = 0;
        DWORD rebootReasons = 0;
        status = RmGetList(sessionHandle, &needed, &count, nullptr, &rebootReasons);
        if (status == ERROR_SUCCESS)
        {
            RmEndSession(sessionHandle);
            return true;
        }

        if (status != ERROR_MORE_DATA)
        {
            RmEndSession(sessionHandle);
            error = L"RmGetList failed (" + std::to_wstring(status) + L")";
            return false;
        }

        std::vector<RM_PROCESS_INFO> infos(needed);
        count = needed;
        status = RmGetList(sessionHandle, &needed, &count, infos.data(), &rebootReasons);
        RmEndSession(sessionHandle);

        if (status != ERROR_SUCCESS)
        {
            error = L"RmGetList details failed (" + std::to_wstring(status) + L")";
            return false;
        }

        std::set<DWORD> dedup;
        for (UINT i = 0; i < count; ++i)
        {
            const DWORD pid = infos[i].Process.dwProcessId;
            if (pid == 0 || pid == 4 || pid == GetCurrentProcessId())
            {
                continue;
            }

            if (dedup.insert(pid).second)
            {
                pids.push_back(pid);
            }
        }

        return true;
    }

} // namespace

namespace utm::tools::process
{

    std::wstring ProcessActions::LastErrorMessage(const wchar_t *context)
    {
        const DWORD code = GetLastError();

        wchar_t *buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            code,
            0,
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        std::wstring message = context;
        message += L" (" + std::to_wstring(code) + L")";

        if (length > 0 && buffer)
        {
            message += L": ";
            message += buffer;
            LocalFree(buffer);
        }

        return message;
    }

    bool ProcessActions::SmartTerminate(DWORD pid, DWORD gracefulTimeoutMs, std::wstring &error)
    {
        HANDLE process = nullptr;
        if (!OpenTargetProcess(pid, PROCESS_TERMINATE | SYNCHRONIZE, process, error))
        {
            error = LastErrorMessage(L"OpenProcess for SmartTerminate failed");
            return false;
        }

        CloseWindowsContext ctx{};
        ctx.pid = pid;
        EnumWindows(CollectWindowsByPid, reinterpret_cast<LPARAM>(&ctx));

        for (HWND window : ctx.windows)
        {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }

        if (WaitForSingleObject(process, gracefulTimeoutMs) == WAIT_OBJECT_0)
        {
            CloseHandle(process);
            return true;
        }

        const BOOL terminated = TerminateProcess(process, 1);
        if (!terminated)
        {
            error = LastErrorMessage(L"TerminateProcess failed");
            CloseHandle(process);
            return false;
        }

        CloseHandle(process);
        return true;
    }

    bool ProcessActions::TerminateTree(DWORD rootPid, std::wstring &error)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            error = LastErrorMessage(L"CreateToolhelp32Snapshot failed");
            return false;
        }

        std::unordered_map<DWORD, std::vector<DWORD>> childrenMap;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                childrenMap[entry.th32ParentProcessID].push_back(entry.th32ProcessID);
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);

        std::vector<DWORD> order;
        std::vector<DWORD> stack{rootPid};

        while (!stack.empty())
        {
            const DWORD current = stack.back();
            stack.pop_back();
            order.push_back(current);

            if (const auto it = childrenMap.find(current); it != childrenMap.end())
            {
                for (const DWORD child : it->second)
                {
                    stack.push_back(child);
                }
            }
        }

        bool ok = true;
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            std::wstring localError;
            if (!SmartTerminate(*it, 1200, localError))
            {
                ok = false;
                error = localError;
            }
        }

        return ok;
    }

    bool ProcessActions::SetPriority(DWORD pid, DWORD priorityClass, std::wstring &error)
    {
        HANDLE process = nullptr;
        if (!OpenTargetProcess(pid, PROCESS_SET_INFORMATION, process, error))
        {
            error = LastErrorMessage(L"OpenProcess for SetPriority failed");
            return false;
        }

        const BOOL ok = SetPriorityClass(process, priorityClass);
        if (!ok)
        {
            error = LastErrorMessage(L"SetPriorityClass failed");
        }

        CloseHandle(process);
        return ok == TRUE;
    }

    bool ProcessActions::SetAffinity(DWORD pid, DWORD_PTR affinityMask, std::wstring &error)
    {
        HANDLE process = nullptr;
        if (!OpenTargetProcess(pid, PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, process, error))
        {
            error = LastErrorMessage(L"OpenProcess for SetAffinity failed");
            return false;
        }

        const BOOL ok = SetProcessAffinityMask(process, affinityMask);
        if (!ok)
        {
            error = LastErrorMessage(L"SetProcessAffinityMask failed");
        }

        CloseHandle(process);
        return ok == TRUE;
    }

    bool ProcessActions::Suspend(DWORD pid, std::wstring &error)
    {
        HANDLE process = nullptr;
        if (!OpenTargetProcess(pid, PROCESS_SUSPEND_RESUME, process, error))
        {
            if (!OpenTargetProcess(pid, PROCESS_QUERY_INFORMATION, process, error))
            {
                error = LastErrorMessage(L"OpenProcess for Suspend failed");
                return false;
            }
        }

        const auto &nt = system::ntapi::NtApi::Instance();
        if (nt.HasSuspendResume())
        {
            const NTSTATUS status = nt.SuspendProcess(process);
            CloseHandle(process);
            if (NT_SUCCESS(status))
            {
                return true;
            }
        }
        else
        {
            CloseHandle(process);
        }

        HANDLE threadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (threadSnapshot == INVALID_HANDLE_VALUE)
        {
            error = LastErrorMessage(L"CreateToolhelp32Snapshot for threads failed");
            return false;
        }

        THREADENTRY32 te{};
        te.dwSize = sizeof(te);

        bool any = false;
        if (Thread32First(threadSnapshot, &te))
        {
            do
            {
                if (te.th32OwnerProcessID != pid)
                {
                    continue;
                }

                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (!thread)
                {
                    continue;
                }

                SuspendThread(thread);
                CloseHandle(thread);
                any = true;
            } while (Thread32Next(threadSnapshot, &te));
        }

        CloseHandle(threadSnapshot);

        if (!any)
        {
            error = L"No suspendable thread found.";
            return false;
        }

        return true;
    }

    bool ProcessActions::Resume(DWORD pid, std::wstring &error)
    {
        HANDLE process = nullptr;
        if (!OpenTargetProcess(pid, PROCESS_SUSPEND_RESUME, process, error))
        {
            if (!OpenTargetProcess(pid, PROCESS_QUERY_INFORMATION, process, error))
            {
                error = LastErrorMessage(L"OpenProcess for Resume failed");
                return false;
            }
        }

        const auto &nt = system::ntapi::NtApi::Instance();
        if (nt.HasSuspendResume())
        {
            const NTSTATUS status = nt.ResumeProcess(process);
            CloseHandle(process);
            if (NT_SUCCESS(status))
            {
                return true;
            }
        }
        else
        {
            CloseHandle(process);
        }

        HANDLE threadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (threadSnapshot == INVALID_HANDLE_VALUE)
        {
            error = LastErrorMessage(L"CreateToolhelp32Snapshot for threads failed");
            return false;
        }

        THREADENTRY32 te{};
        te.dwSize = sizeof(te);

        bool any = false;
        if (Thread32First(threadSnapshot, &te))
        {
            do
            {
                if (te.th32OwnerProcessID != pid)
                {
                    continue;
                }

                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
                if (!thread)
                {
                    continue;
                }

                while (ResumeThread(thread) > 0)
                {
                }

                CloseHandle(thread);
                any = true;
            } while (Thread32Next(threadSnapshot, &te));
        }

        CloseHandle(threadSnapshot);

        if (!any)
        {
            error = L"No resumable thread found.";
            return false;
        }

        return true;
    }

    bool ProcessActions::OpenFileLocation(DWORD pid, std::wstring &error)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
        {
            error = LastErrorMessage(L"OpenProcess for OpenFileLocation failed");
            return false;
        }

        wchar_t path[MAX_PATH]{};
        DWORD size = static_cast<DWORD>(std::size(path));
        if (!QueryFullProcessImageNameW(process, 0, path, &size))
        {
            error = LastErrorMessage(L"QueryFullProcessImageNameW failed");
            CloseHandle(process);
            return false;
        }

        CloseHandle(process);

        std::wstring args = L"/select,\"";
        args += path;
        args += L"\"";

        const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            error = L"Failed to open file location.";
            return false;
        }

        return true;
    }

    bool ProcessActions::OpenProperties(DWORD pid, std::wstring &error)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
        {
            error = LastErrorMessage(L"OpenProcess for OpenProperties failed");
            return false;
        }

        wchar_t path[MAX_PATH]{};
        DWORD size = static_cast<DWORD>(std::size(path));
        if (!QueryFullProcessImageNameW(process, 0, path, &size))
        {
            error = LastErrorMessage(L"QueryFullProcessImageNameW failed");
            CloseHandle(process);
            return false;
        }

        CloseHandle(process);

        const HINSTANCE result = ShellExecuteW(nullptr, L"properties", path, nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            error = L"Failed to open properties dialog.";
            return false;
        }

        return true;
    }

    bool ProcessActions::ForceDeletePath(const std::wstring &path, bool isDirectory, std::wstring &error)
    {
        if (path.empty())
        {
            error = L"No target path selected.";
            return false;
        }

        const std::filesystem::path target(path);

        if (TryDeletePathNow(target, isDirectory, error))
        {
            return true;
        }

        std::vector<DWORD> lockingPids;
        std::wstring lockError;
        if (!GetLockingPids(path, lockingPids, lockError) && !lockError.empty())
        {
            error = lockError;
        }

        for (const DWORD pid : lockingPids)
        {
            std::wstring killError;
            if (!TerminateTree(pid, killError))
            {
                (void)SmartTerminate(pid, 1500, killError);
            }
        }

        Sleep(250);

        if (TryDeletePathNow(target, isDirectory, error))
        {
            return true;
        }

        if (!lockError.empty())
        {
            error += L"\n";
            error += lockError;
        }

        return false;
    }

} // namespace utm::tools::process
