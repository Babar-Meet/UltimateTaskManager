#include "system/process/ProcessEnumerator.h"

#include "system/ntapi/NtApi.h"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>

namespace
{

#pragma pack(push, 8)
    struct SystemProcessInformationRecord
    {
        ULONG NextEntryOffset;
        ULONG NumberOfThreads;
        LARGE_INTEGER WorkingSetPrivateSize;
        ULONG HardFaultCount;
        ULONG NumberOfThreadsHighWatermark;
        ULONGLONG CycleTime;
        LARGE_INTEGER CreateTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER KernelTime;
        UNICODE_STRING ImageName;
        KPRIORITY BasePriority;
        HANDLE UniqueProcessId;
        HANDLE InheritedFromUniqueProcessId;
        ULONG HandleCount;
        ULONG SessionId;
        ULONG_PTR UniqueProcessKey;
        SIZE_T PeakVirtualSize;
        SIZE_T VirtualSize;
        ULONG PageFaultCount;
        SIZE_T PeakWorkingSetSize;
        SIZE_T WorkingSetSize;
        SIZE_T QuotaPeakPagedPoolUsage;
        SIZE_T QuotaPagedPoolUsage;
        SIZE_T QuotaPeakNonPagedPoolUsage;
        SIZE_T QuotaNonPagedPoolUsage;
        SIZE_T PagefileUsage;
        SIZE_T PeakPagefileUsage;
        SIZE_T PrivatePageCount;
        LARGE_INTEGER ReadOperationCount;
        LARGE_INTEGER WriteOperationCount;
        LARGE_INTEGER OtherOperationCount;
        LARGE_INTEGER ReadTransferCount;
        LARGE_INTEGER WriteTransferCount;
        LARGE_INTEGER OtherTransferCount;
    };
#pragma pack(pop)

    std::wstring ResolveProcessName(DWORD pid, const UNICODE_STRING &name)
    {
        if (name.Buffer && name.Length > 0)
        {
            return std::wstring(name.Buffer, name.Length / sizeof(wchar_t));
        }

        if (pid == 0)
        {
            return L"System Idle Process";
        }

        if (pid == 4)
        {
            return L"System";
        }

        return L"<unknown>";
    }

    std::uint64_t U64FromLargeInteger(const LARGE_INTEGER &value)
    {
        return static_cast<std::uint64_t>(value.QuadPart);
    }

} // namespace

namespace utm::system::process
{

    ProcessEnumerator::ProcessEnumerator()
    {
        ntReady_ = ntapi::NtApi::Instance().Initialize();
    }

    std::vector<RawProcessInfo> ProcessEnumerator::Enumerate()
    {
        if (ntReady_)
        {
            auto result = EnumerateWithNtApi();
            if (!result.empty())
            {
                lastUsedNt_ = true;
                return result;
            }
        }

        lastUsedNt_ = false;
        return EnumerateWithToolHelp();
    }

    bool ProcessEnumerator::LastEnumerationUsedNtApi() const
    {
        return lastUsedNt_;
    }

    std::vector<RawProcessInfo> ProcessEnumerator::EnumerateWithNtApi()
    {
        constexpr ULONG kInitialBuffer = 1 * 1024 * 1024;

        ULONG bufferSize = kInitialBuffer;
        std::vector<std::uint8_t> buffer(bufferSize);

        ULONG returnLength = 0;
        NTSTATUS status = STATUS_UNSUCCESSFUL;

        for (int attempt = 0; attempt < 8; ++attempt)
        {
            status = ntapi::NtApi::Instance().QuerySystemInformation(
                SystemProcessInformation,
                buffer.data(),
                bufferSize,
                &returnLength);

            if (NT_SUCCESS(status))
            {
                break;
            }

            if (status == STATUS_INFO_LENGTH_MISMATCH)
            {
                bufferSize = returnLength > bufferSize ? returnLength + 64 * 1024 : bufferSize * 2;
                buffer.resize(bufferSize);
                continue;
            }

            return {};
        }

        if (!NT_SUCCESS(status))
        {
            return {};
        }

        std::vector<RawProcessInfo> list;
        const auto *base = buffer.data();

        ULONG offset = 0;
        while (true)
        {
            const auto *record = reinterpret_cast<const SystemProcessInformationRecord *>(base + offset);

            RawProcessInfo item;
            item.pid = static_cast<std::uint32_t>(reinterpret_cast<ULONG_PTR>(record->UniqueProcessId));
            item.parentPid = static_cast<std::uint32_t>(reinterpret_cast<ULONG_PTR>(record->InheritedFromUniqueProcessId));
            item.imageName = ResolveProcessName(item.pid, record->ImageName);

            item.threadCount = record->NumberOfThreads;
            item.handleCount = record->HandleCount;
            item.basePriority = record->BasePriority;

            item.workingSetBytes = static_cast<std::uint64_t>(record->WorkingSetSize);
            item.privateBytes = static_cast<std::uint64_t>(record->PrivatePageCount);
            item.readBytes = U64FromLargeInteger(record->ReadTransferCount);
            item.writeBytes = U64FromLargeInteger(record->WriteTransferCount);
            item.kernelTime100ns = U64FromLargeInteger(record->KernelTime);
            item.userTime100ns = U64FromLargeInteger(record->UserTime);
            item.createTime100ns = U64FromLargeInteger(record->CreateTime);

            list.push_back(std::move(item));

            if (record->NextEntryOffset == 0)
            {
                break;
            }

            offset += record->NextEntryOffset;
        }

        return list;
    }

    std::vector<RawProcessInfo> ProcessEnumerator::EnumerateWithToolHelp()
    {
        std::vector<RawProcessInfo> list;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return list;
        }

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        if (!Process32FirstW(snapshot, &entry))
        {
            CloseHandle(snapshot);
            return list;
        }

        do
        {
            RawProcessInfo item;
            item.pid = entry.th32ProcessID;
            item.parentPid = entry.th32ParentProcessID;
            item.imageName = entry.szExeFile;
            item.threadCount = entry.cntThreads;
            item.basePriority = entry.pcPriClassBase;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, item.pid);
            if (process)
            {
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
                {
                    item.workingSetBytes = static_cast<std::uint64_t>(pmc.WorkingSetSize);
                    item.privateBytes = static_cast<std::uint64_t>(pmc.PrivateUsage);
                }

                IO_COUNTERS io{};
                if (GetProcessIoCounters(process, &io))
                {
                    item.readBytes = io.ReadTransferCount;
                    item.writeBytes = io.WriteTransferCount;
                }

                FILETIME create{}, exit{}, kernel{}, user{};
                if (GetProcessTimes(process, &create, &exit, &kernel, &user))
                {
                    ULARGE_INTEGER uKernel{};
                    uKernel.LowPart = kernel.dwLowDateTime;
                    uKernel.HighPart = kernel.dwHighDateTime;
                    item.kernelTime100ns = uKernel.QuadPart;

                    ULARGE_INTEGER uUser{};
                    uUser.LowPart = user.dwLowDateTime;
                    uUser.HighPart = user.dwHighDateTime;
                    item.userTime100ns = uUser.QuadPart;

                    ULARGE_INTEGER uCreate{};
                    uCreate.LowPart = create.dwLowDateTime;
                    uCreate.HighPart = create.dwHighDateTime;
                    item.createTime100ns = uCreate.QuadPart;
                }

                CloseHandle(process);
            }

            list.push_back(std::move(item));
        } while (Process32NextW(snapshot, &entry));

        CloseHandle(snapshot);
        return list;
    }

} // namespace utm::system::process
