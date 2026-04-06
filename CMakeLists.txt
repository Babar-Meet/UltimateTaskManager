cmake_minimum_required(VERSION 3.24)
project(UltimateTaskManager LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /EHsc /utf-8)
    add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

set(SOURCES
    src/app/main.cpp
    src/core/engine/MonitorEngine.cpp
    src/system/ntapi/NtApi.cpp
    src/system/process/ProcessEnumerator.cpp
    src/system/services/ServiceManager.cpp
    src/tools/process/ProcessActions.cpp
    src/ui/MainWindow.cpp
    src/ui/SidebarNavigation.cpp
    src/ui/SidebarRetractable.cpp
    src/ui/ProcessListView.cpp
    src/ui/PerformancePanel.cpp
    src/ui/NetworkPanel.cpp
    src/ui/HardwarePanel.cpp
    src/ui/ServicesPanel.cpp
    src/ui/QuickToolsPanel.cpp
    src/ui/StatusBar.cpp
    src/util/logging/Logger.cpp
    src/util/security/PrivilegeManager.cpp
    resources/UltimateTaskManager.rc
)

add_executable(UltimateTaskManager WIN32 ${SOURCES})

target_include_directories(UltimateTaskManager PRIVATE include)

target_link_libraries(UltimateTaskManager PRIVATE
    advapi32
    comdlg32
    comctl32
    d2d1
    dxgi
    gdi32
    iphlpapi
    pdh
    psapi
    rstrtmgr
    setupapi
    cfgmgr32
    shell32
    shlwapi
    user32
    ws2_32
)

if(MSVC)
    target_link_options(UltimateTaskManager PRIVATE "/MANIFEST:NO")
endif()
