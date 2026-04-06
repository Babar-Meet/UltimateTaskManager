#include "tools/process/ProcessActions.h"

#include "system/ntapi/NtApi.h"

#include <RestartManager.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <shellapi.h>
#include <set>
#include <tlhelp32.h>
#include <unordered_map>
#include <vector>

namespace
{

    constexpr size_t kMaxRestartManagerResources = 2048;
    constexpr DWORD kDeleteRetryDelayMs = 180;
    constexpr int kDeleteRetryCount = 6;

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

    bool TryStagePathForDeletion(const std::filesystem::path &target, std::filesystem::path &stagedPath, std::wstring &error)
    {
        stagedPath.clear();
        error.clear();

        const std::filesystem::path parent = target.parent_path();
        if (parent.empty())
        {
            return false;
        }

        const std::wstring baseName = target.filename().wstring();
        const unsigned long long seed = static_cast<unsigned long long>(GetTickCount64());

        std::error_code lastEc;
        for (int attempt = 0; attempt < 24; ++attempt)
        {
            std::filesystem::path candidate = parent / (baseName + L".utm_force_delete_" +
                                                        std::to_wstring(seed) + L"_" + std::to_wstring(attempt));

            std::error_code ec;
            std::filesystem::rename(target, candidate, ec);
            if (!ec)
            {
                stagedPath = std::move(candidate);
                return true;
            }

            lastEc = ec;
        }

        if (lastEc)
        {
            const std::string raw = lastEc.message();
            error = L"Failed to stage target for forced deletion by rename: " + std::wstring(raw.begin(), raw.end());
        }
        else
        {
            error = L"Failed to stage target for forced deletion by rename.";
        }
        return false;
    }

    std::wstring Widen(const std::string &text)
    {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring ToLowerWide(const std::wstring &text)
    {
        std::wstring lowered = text;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(std::towlower(ch)); });
        return lowered;
    }

    std::wstring NormalizePathForCompare(const std::filesystem::path &path)
    {
        std::error_code ec;
        std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
        if (ec)
        {
            normalized = path.lexically_normal();
        }

        std::wstring text = normalized.wstring();
        std::replace(text.begin(), text.end(), L'/', L'\\');

        while (text.size() > 3 && !text.empty() && text.back() == L'\\')
        {
            text.pop_back();
        }

        return ToLowerWide(text);
    }

    bool IsPathInsideDirectory(const std::wstring &normalizedPath, const std::wstring &normalizedDirectory)
    {
        if (normalizedDirectory.empty())
        {
            return false;
        }

        if (normalizedPath.size() <= normalizedDirectory.size())
        {
            return false;
        }

        if (normalizedPath.compare(0, normalizedDirectory.size(), normalizedDirectory) != 0)
        {
            return false;
        }

        return normalizedPath[normalizedDirectory.size()] == L'\\';
    }

    void AppendMessageLine(std::wstring &message, const std::wstring &line)
    {
        if (line.empty())
        {
            return;
        }

        if (!message.empty())
        {
            message += L"\n";
        }

        message += line;
    }

    void ClearReadonlyAttributeIfNeeded(const std::filesystem::path &path)
    {
        const std::wstring native = path.wstring();
        const DWORD attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return;
        }

        if ((attributes & FILE_ATTRIBUTE_READONLY) == 0)
        {
            return;
        }

        (void)SetFileAttributesW(native.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }

    void NormalizeDeletionAttributes(const std::filesystem::path &target, bool isDirectory)
    {
        ClearReadonlyAttributeIfNeeded(target);

        if (!isDirectory)
        {
            return;
        }

        std::error_code ec;
        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator it(target, options, ec);
        const std::filesystem::recursive_directory_iterator end;

        for (; it != end && !ec; it.increment(ec))
        {
            ClearReadonlyAttributeIfNeeded(it->path());
        }
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

    void BuildRestartManagerResources(const std::filesystem::path &target, bool isDirectory, std::vector<std::wstring> &resources, std::wstring &warning)
    {
        resources.clear();
        warning.clear();

        if (!isDirectory)
        {
            resources.push_back(target.wstring());
            return;
        }

        std::error_code ec;
        if (!std::filesystem::exists(target, ec))
        {
            if (ec)
            {
                warning = L"Directory scan warning: " + Widen(ec.message());
            }
            return;
        }

        const auto options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator it(target, options, ec);
        const std::filesystem::recursive_directory_iterator end;

        for (; it != end && !ec; it.increment(ec))
        {
            std::error_code typeError;
            if (!std::filesystem::is_regular_file(it->path(), typeError))
            {
                continue;
            }

            resources.push_back(it->path().wstring());
            if (resources.size() >= kMaxRestartManagerResources)
            {
                warning = L"Restart Manager scan limit reached; scanned first " + std::to_wstring(kMaxRestartManagerResources) + L" files.";
                break;
            }
        }

        if (resources.empty())
        {
            warning = L"Directory lock scan had no file entries; using process-kill fallback heuristics.";
        }
        else if (ec && warning.empty())
        {
            warning = L"Some directory entries could not be scanned for lock ownership.";
        }
    }

    void AddExplorerProcessCandidates(std::vector<DWORD> &pids)
    {
        const DWORD selfPid = GetCurrentProcessId();
        std::set<DWORD> dedup(pids.begin(), pids.end());

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                const DWORD pid = entry.th32ProcessID;
                if (pid == 0 || pid == 4 || pid == selfPid)
                {
                    continue;
                }

                if (_wcsicmp(entry.szExeFile, L"explorer.exe") != 0)
                {
                    continue;
                }

                if (dedup.insert(pid).second)
                {
                    pids.push_back(pid);
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
    }

    void KillPidCandidates(const std::vector<DWORD> &pids, int &killedCount, int &killFailedCount)
    {
        killedCount = 0;
        killFailedCount = 0;

        for (const DWORD pid : pids)
        {
            std::wstring killError;
            if (!utm::tools::process::ProcessActions::TerminateTree(pid, killError))
            {
                if (!utm::tools::process::ProcessActions::SmartTerminate(pid, 1500, killError))
                {
                    ++killFailedCount;
                    continue;
                }
            }

            ++killedCount;
        }
    }

    bool RelaunchExplorerShell()
    {
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    bool GetLockingPids(const std::vector<std::wstring> &resources, std::vector<DWORD> &pids, std::wstring &error)
    {
        pids.clear();
        error.clear();

        if (resources.empty())
        {
            return true;
        }

        DWORD sessionHandle = 0;
        WCHAR sessionKey[CCH_RM_SESSION_KEY + 1]{};
        DWORD status = RmStartSession(&sessionHandle, 0, sessionKey);
        if (status != ERROR_SUCCESS)
        {
            error = L"RmStartSession failed (" + std::to_wstring(status) + L")";
            return false;
        }

        constexpr size_t kRegisterChunkSize = 64;
        std::vector<LPCWSTR> chunk;
        chunk.reserve(kRegisterChunkSize);

        for (size_t offset = 0; offset < resources.size();)
        {
            chunk.clear();
            const size_t count = (std::min)(kRegisterChunkSize, resources.size() - offset);
            for (size_t i = 0; i < count; ++i)
            {
                chunk.push_back(resources[offset + i].c_str());
            }

            status = RmRegisterResources(sessionHandle, static_cast<UINT>(chunk.size()), chunk.data(), 0, nullptr, 0, nullptr);
            if (status != ERROR_SUCCESS)
            {
                RmEndSession(sessionHandle);
                error = L"RmRegisterResources failed (" + std::to_wstring(status) + L")";
                return false;
            }

            offset += count;
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

    bool TryGetProcessImagePath(DWORD pid, std::wstring &imagePath)
    {
        imagePath.clear();

        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
        {
            return false;
        }

        std::vector<wchar_t> buffer(32768, L'\0');
        DWORD size = static_cast<DWORD>(buffer.size());
        const BOOL ok = QueryFullProcessImageNameW(process, 0, buffer.data(), &size);
        CloseHandle(process);

        if (!ok || size == 0)
        {
            return false;
        }

        imagePath.assign(buffer.data(), size);
        return true;
    }

    void AddPathRelatedProcessCandidates(const std::filesystem::path &target, bool isDirectory, std::vector<DWORD> &pids)
    {
        const DWORD selfPid = GetCurrentProcessId();
        std::set<DWORD> dedup(pids.begin(), pids.end());

        const std::wstring normalizedTarget = NormalizePathForCompare(target);
        const std::wstring normalizedDirectory = isDirectory ? normalizedTarget : NormalizePathForCompare(target.parent_path());

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                const DWORD pid = entry.th32ProcessID;
                if (pid == 0 || pid == 4 || pid == selfPid)
                {
                    continue;
                }

                std::wstring imagePath;
                if (!TryGetProcessImagePath(pid, imagePath))
                {
                    continue;
                }

                const std::wstring normalizedImage = NormalizePathForCompare(std::filesystem::path(imagePath));

                bool match = false;
                if (isDirectory)
                {
                    match = IsPathInsideDirectory(normalizedImage, normalizedDirectory);
                }
                else
                {
                    match = normalizedImage == normalizedTarget;
                }

                if (match && dedup.insert(pid).second)
                {
                    pids.push_back(pid);
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
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

        NormalizeDeletionAttributes(target, isDirectory);

        if (TryDeletePathNow(target, isDirectory, error))
        {
            return true;
        }

        std::vector<std::wstring> restartManagerResources;
        std::wstring scanWarning;
        BuildRestartManagerResources(target, isDirectory, restartManagerResources, scanWarning);

        std::vector<DWORD> lockingPids;
        std::wstring lockError;
        if (!GetLockingPids(restartManagerResources, lockingPids, lockError) && !lockError.empty())
        {
            AppendMessageLine(error, lockError);
        }

        AddPathRelatedProcessCandidates(target, isDirectory, lockingPids);

        int killedCount = 0;
        int killFailedCount = 0;
        std::filesystem::path deleteTarget = target;
        bool usedStagingTarget = false;

        KillPidCandidates(lockingPids, killedCount, killFailedCount);

        for (int attempt = 0; attempt < kDeleteRetryCount; ++attempt)
        {
            NormalizeDeletionAttributes(deleteTarget, isDirectory);
            if (TryDeletePathNow(deleteTarget, isDirectory, error))
            {
                return true;
            }

            if (attempt + 1 < kDeleteRetryCount)
            {
                Sleep(kDeleteRetryDelayMs);
            }
        }

        std::filesystem::path stagedTarget;
        std::wstring stageError;
        if (TryStagePathForDeletion(deleteTarget, stagedTarget, stageError))
        {
            deleteTarget = stagedTarget;
            usedStagingTarget = true;

            for (int attempt = 0; attempt < kDeleteRetryCount; ++attempt)
            {
                NormalizeDeletionAttributes(deleteTarget, isDirectory);
                if (TryDeletePathNow(deleteTarget, isDirectory, error))
                {
                    return true;
                }

                if (attempt + 1 < kDeleteRetryCount)
                {
                    Sleep(kDeleteRetryDelayMs);
                }
            }
        }

        bool triedExplorerFallback = false;
        int explorerKilledCount = 0;
        int explorerKillFailedCount = 0;
        if (isDirectory && lockingPids.empty())
        {
            std::vector<DWORD> explorerPids;
            AddExplorerProcessCandidates(explorerPids);
            if (!explorerPids.empty())
            {
                triedExplorerFallback = true;
                KillPidCandidates(explorerPids, explorerKilledCount, explorerKillFailedCount);

                Sleep(250);
                for (int attempt = 0; attempt < kDeleteRetryCount; ++attempt)
                {
                    NormalizeDeletionAttributes(deleteTarget, isDirectory);
                    if (TryDeletePathNow(deleteTarget, isDirectory, error))
                    {
                        (void)RelaunchExplorerShell();
                        return true;
                    }

                    if (attempt + 1 < kDeleteRetryCount)
                    {
                        Sleep(kDeleteRetryDelayMs);
                    }
                }

                (void)RelaunchExplorerShell();
            }
        }

        std::wstring diagnostics;
        if (!scanWarning.empty())
        {
            AppendMessageLine(diagnostics, scanWarning);
        }

        if (!stageError.empty())
        {
            AppendMessageLine(diagnostics, stageError);
        }

        if (usedStagingTarget)
        {
            AppendMessageLine(diagnostics, L"Deletion was attempted on staged path: " + deleteTarget.wstring());
        }

        if (!lockingPids.empty())
        {
            std::wstring killSummary = L"Lock-owner process candidates: " + std::to_wstring(lockingPids.size()) +
                                       L", terminated: " + std::to_wstring(killedCount) +
                                       L", failed: " + std::to_wstring(killFailedCount) + L".";
            AppendMessageLine(diagnostics, killSummary);
        }
        else
        {
            AppendMessageLine(diagnostics, L"No lock-owner process could be identified automatically.");
        }

        if (triedExplorerFallback)
        {
            std::wstring shellSummary = L"Explorer fallback attempted. Terminated: " + std::to_wstring(explorerKilledCount) +
                                        L", failed: " + std::to_wstring(explorerKillFailedCount) + L".";
            AppendMessageLine(diagnostics, shellSummary);
        }

        if (!lockError.empty())
        {
            AppendMessageLine(diagnostics, lockError);
        }

        if (!diagnostics.empty())
        {
            if (error.empty())
            {
                error = diagnostics;
            }
            else
            {
                error += L"\n";
                error += diagnostics;
            }
        }

        return false;
    }

} // namespace utm::tools::process
