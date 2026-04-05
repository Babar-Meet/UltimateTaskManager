#include "system/ntapi/NtApi.h"

namespace utm::system::ntapi
{

    NtApi &NtApi::Instance()
    {
        static NtApi api;
        return api;
    }

    bool NtApi::Initialize()
    {
        if (initialized_)
        {
            return querySystemInformation_ != nullptr;
        }

        module_ = GetModuleHandleW(L"ntdll.dll");
        if (!module_)
        {
            module_ = LoadLibraryW(L"ntdll.dll");
        }

        if (!module_)
        {
            initialized_ = true;
            return false;
        }

        querySystemInformation_ = reinterpret_cast<NtQuerySystemInformationFn>(
            GetProcAddress(module_, "NtQuerySystemInformation"));
        suspendProcess_ = reinterpret_cast<NtSuspendProcessFn>(GetProcAddress(module_, "NtSuspendProcess"));
        resumeProcess_ = reinterpret_cast<NtResumeProcessFn>(GetProcAddress(module_, "NtResumeProcess"));

        initialized_ = true;
        return querySystemInformation_ != nullptr;
    }

    bool NtApi::IsAvailable() const
    {
        return querySystemInformation_ != nullptr;
    }

    NTSTATUS NtApi::QuerySystemInformation(
        SYSTEM_INFORMATION_CLASS informationClass,
        PVOID buffer,
        ULONG bufferSize,
        PULONG returnLength) const
    {
        if (!querySystemInformation_)
        {
            return STATUS_NOT_IMPLEMENTED;
        }

        return querySystemInformation_(informationClass, buffer, bufferSize, returnLength);
    }

    NTSTATUS NtApi::SuspendProcess(HANDLE processHandle) const
    {
        if (!suspendProcess_)
        {
            return STATUS_NOT_IMPLEMENTED;
        }

        return suspendProcess_(processHandle);
    }

    NTSTATUS NtApi::ResumeProcess(HANDLE processHandle) const
    {
        if (!resumeProcess_)
        {
            return STATUS_NOT_IMPLEMENTED;
        }

        return resumeProcess_(processHandle);
    }

    bool NtApi::HasSuspendResume() const
    {
        return suspendProcess_ != nullptr && resumeProcess_ != nullptr;
    }

} // namespace utm::system::ntapi
