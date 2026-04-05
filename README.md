# Ultimate Task Manager (Native Win32 / C++)

Production-oriented Windows Task Manager-style utility built in modern C++ with a pure Win32 UI and an NT API-first collector pipeline.

## Current Implementation Scope

This initial implementation starts the full architecture and includes:

- Modular project layout with separate layer boundaries:
  - `include/core`, `include/system`, `include/ui`, `include/tools`, `include/util`
  - `src/core`, `src/system`, `src/ui`, `src/tools`, `src/util`
- NT API loader (`NtQuerySystemInformation`, `NtSuspendProcess`, `NtResumeProcess`) with runtime fallback behavior
- Process collector engine running on a background worker thread (`800ms` refresh)
- Process list UI (owner-data list view, sortable, searchable)
- Process control context menu:
  - End Task (smart graceful to force)
  - End Process Tree
  - Set Priority (Idle to Realtime)
  - Set Affinity (all/selected cores)
  - Suspend/Resume
  - Open File Location
  - Properties
- UAC manifest (`requireAdministrator`)
- SeDebugPrivilege enable attempt with graceful fallback
- Centralized file logger
- Left sidebar and tabbed shell (Processes, Performance, Network, Hardware)

## Directory Structure

```text
UltimateTaskManager/
  CMakeLists.txt
  README.md
  .gitignore
  resources/
    app.manifest
    UltimateTaskManager.rc
  include/
    core/
      engine/
      model/
    system/
      ntapi/
      process/
    tools/
      process/
    ui/
    util/
      logging/
      security/
  src/
    app/
    core/
      engine/
    system/
      ntapi/
      process/
    tools/
      process/
    ui/
    util/
      logging/
      security/
```

## Build Requirements

- Windows 10/11
- Visual Studio 2022 (MSVC v143) or newer
- CMake 3.24+
- x64 target

## Build in Visual Studio (Recommended)

## One-Click Scripts (Easy Mode)

From the repository root, you can use these BAT files:

- `setup_build.bat [Debug|Release]`
  - Configures and builds the app.
  - Auto-detects `cmake.exe` from PATH and common install folders.
- `run_utm.bat [Debug|Release]`
  - Runs the built app as Administrator.
  - If the binary is missing, it triggers `setup_build.bat` first.
- `setup_and_run.bat [Debug|Release]`
  - Build + run in one command.

Examples:

```bat
setup_and_run.bat
setup_and_run.bat Debug
```

If CMake is missing, install it with:

```powershell
winget install Kitware.CMake
```

Then reopen your terminal and run the BAT file again.

### Option A: Open Folder (CMake)

1. Open Visual Studio.
2. `File` -> `Open` -> `Folder...` and select this repository root.
3. Ensure configuration is `x64-Debug` or `x64-Release`.
4. Build `UltimateTaskManager`.
5. Run with administrator rights.

### Option B: Command Line

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Executable output:

- `build/Release/UltimateTaskManager.exe`

## Linked System Libraries

Configured in `CMakeLists.txt`:

- `advapi32`
- `comctl32`
- `d2d1`
- `dxgi`
- `gdi32`
- `iphlpapi`
- `psapi`
- `rstrtmgr`
- `setupapi`
- `cfgmgr32`
- `shell32`
- `shlwapi`
- `user32`
- `ws2_32`

## Manifest / Privileges

- Manifest file: `resources/app.manifest`
- Embedded through: `resources/UltimateTaskManager.rc`
- UAC level: `requireAdministrator`
- Runtime privilege path:
  - Attempt to enable `SeDebugPrivilege`
  - Continue in degraded mode if privilege is unavailable

## Notes on Performance Model

- Non-blocking UI: collection happens on a worker thread
- Snapshot delivery: engine posts updates to UI via `WM_APP` message
- Process list uses owner-data (`LVS_OWNERDATA`) for high row-count scalability

## Next Implementation Milestones

1. Direct2D real-time detachable graph subsystem (with GDI fallback)
2. Network deep-dive tab (TCP/UDP connection mapping and throughput views)
3. Hardware insight tab (USB, storage, adapter, GPU, peripheral details)
4. Advanced tools:
   - Port-based killer
   - Pattern/regex killer
   - Smart Delete++ (Restart Manager)
5. Service manager and startup applications viewer
6. Snapshot export (JSON), process tree view, spike/leak analytics
