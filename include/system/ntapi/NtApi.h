#pragma once

#include <windows.h>
#include <winternl.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif

#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif

namespace utm::system::ntapi
{

    using NtQuerySystemInformationFn = NTSTATUS(NTAPI *)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    using NtSuspendProcessFn = NTSTATUS(NTAPI *)(HANDLE);
    using NtResumeProcessFn = NTSTATUS(NTAPI *)(HANDLE);

    class NtApi
    {
    public:
        static NtApi &Instance();

        bool Initialize();
        bool IsAvailable() const;

        NTSTATUS QuerySystemInformation(
            SYSTEM_INFORMATION_CLASS informationClass,
            PVOID buffer,
            ULONG bufferSize,
            PULONG returnLength) const;

        NTSTATUS SuspendProcess(HANDLE processHandle) const;
        NTSTATUS ResumeProcess(HANDLE processHandle) const;

        bool HasSuspendResume() const;

    private:
        NtApi() = default;

        HMODULE module_ = nullptr;
        NtQuerySystemInformationFn querySystemInformation_ = nullptr;
        NtSuspendProcessFn suspendProcess_ = nullptr;
        NtResumeProcessFn resumeProcess_ = nullptr;
        bool initialized_ = false;
    };

} // namespace utm::system::ntapi
