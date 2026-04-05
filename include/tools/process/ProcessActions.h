#pragma once

#include <windows.h>

#include <string>

namespace utm::tools::process
{

    class ProcessActions
    {
    public:
        static bool SmartTerminate(DWORD pid, DWORD gracefulTimeoutMs, std::wstring &error);
        static bool TerminateTree(DWORD rootPid, std::wstring &error);

        static bool SetPriority(DWORD pid, DWORD priorityClass, std::wstring &error);
        static bool SetAffinity(DWORD pid, DWORD_PTR affinityMask, std::wstring &error);

        static bool Suspend(DWORD pid, std::wstring &error);
        static bool Resume(DWORD pid, std::wstring &error);

        static bool OpenFileLocation(DWORD pid, std::wstring &error);
        static bool OpenProperties(DWORD pid, std::wstring &error);

    private:
        static std::wstring LastErrorMessage(const wchar_t *context);
    };

} // namespace utm::tools::process
