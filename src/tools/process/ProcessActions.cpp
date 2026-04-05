#include "tools/process/ProcessActions.h"

#include "system/ntapi/NtApi.h"

#include <tlhelp32.h>
#include <shellapi.h>
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

} // namespace utm::tools::process
