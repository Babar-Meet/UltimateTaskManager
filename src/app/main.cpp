#include "ui/MainWindow.h"

#include "util/logging/Logger.h"
#include "util/security/PrivilegeManager.h"

#include <windows.h>
#include <commctrl.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    utm::util::logging::Logger::Instance().Initialize();

    LoadLibraryW(L"comctl32.dll");
    InitCommonControls();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&icc))
    {
        const DWORD err = GetLastError();
        utm::util::logging::Logger::Instance().Write(
            utm::util::logging::LogLevel::Warning,
            L"InitCommonControlsEx failed. LastError=" + std::to_wstring(err));
    }

    const auto privilege = utm::util::security::PrivilegeManager::InitializePrivileges();

    utm::util::logging::Logger::Instance().Write(
        utm::util::logging::LogLevel::Info,
        privilege.seDebugEnabled ? L"SeDebugPrivilege enabled." : L"SeDebugPrivilege unavailable; running with graceful fallback.");

    utm::core::model::RuntimeState runtime{};
    runtime.isElevated = privilege.isElevated;
    runtime.seDebugEnabled = privilege.seDebugEnabled;

    utm::ui::MainWindow window(instance, runtime);
    if (!window.CreateAndShow(showCommand))
    {
        MessageBoxW(nullptr, L"Failed to initialize Ultimate Task Manager.", L"Startup Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    return window.RunMessageLoop();
}
