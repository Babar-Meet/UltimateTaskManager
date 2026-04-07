#pragma once

#include "core/engine/MonitorEngine.h"
#include "core/model/ProcessSnapshot.h"
#include "system/services/ServiceManager.h"
#include "system/startup/StartupAppsManager.h"
#include "system/users/UserSessionManager.h"
#include "ui/HardwarePanel.h"
#include "ui/NetworkPanel.h"
#include "ui/PerformancePanel.h"
#include "ui/ProcessListView.h"
#include "ui/QuickToolsPanel.h"
#include "ui/ServicesPanel.h"
#include "ui/SidebarNavigation.h"
#include "ui/SidebarRetractable.h"
#include "ui/StartupAppsPanel.h"
#include "ui/StatusBar.h"
#include "ui/UsersPanel.h"

#include <windows.h>
#include <pdh.h>
#include <dxgi1_6.h>

#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace utm::ui
{

    class MainWindow
    {
    public:
        static constexpr UINT kSnapshotMessage = WM_APP + 42;

        MainWindow(HINSTANCE instance, core::model::RuntimeState runtimeState);
        ~MainWindow();

        MainWindow(const MainWindow &) = delete;
        MainWindow &operator=(const MainWindow &) = delete;

        bool CreateAndShow(int showCommand);
        int RunMessageLoop();

    private:
        friend class SidebarNavigation;
        friend class SidebarRetractable;
        friend class ProcessListView;
        friend class PerformancePanel;
        friend class NetworkPanel;
        friend class HardwarePanel;
        friend class ServicesPanel;
        friend class StartupAppsPanel;
        friend class UsersPanel;
        friend class QuickToolsPanel;
        friend class StatusBar;

        enum class SortColumn
        {
            Name,
            Pid,
            Cpu,
            WorkingSet,
            PrivateBytes,
            Threads,
            Handles,
            ParentPid,
            Priority,
            ReadBytes,
            WriteBytes
        };

        enum class Section
        {
            Processes,
            Performance,
            Network,
            Hardware,
            Services,
            StartupApps,
            Users,
            QuickKillTools
        };

        enum class PerformanceView
        {
            All,
            Cpu,
            Memory,
            Disk,
            Wifi,
            Ethernet,
            Gpu
        };

        enum class CpuGraphMode
        {
            Single,
            PerCore
        };

        enum class NetworkFilterMode
        {
            All,
            Tcp,
            Udp,
            Listening,
            Established
        };

        enum class ServiceFilterMode
        {
            All,
            Running,
            Stopped
        };

        enum class StartupAppsFilterMode
        {
            All,
            Enabled,
            Disabled
        };

        enum class UsersFilterMode
        {
            All,
            Active,
            Disconnected
        };

        struct NetworkInterfacePerf;
        struct GpuPerf;
        struct PerformanceSubviewBinding;
        struct HardwareDeviceEntry;

        static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        bool RegisterWindowClass();
        bool CreateChildControls();
        void LayoutControls();
        void ApplySectionVisibility();
        void SetActiveSection(Section section);
        void SetActivePerformanceView(PerformanceView view);
        void SetCpuGraphMode(CpuGraphMode mode);
        void UpdatePerformanceSubviewSelection();
        void RebuildPerformanceSubviewNavigation();
        bool HandleDynamicPerformanceSubviewCommand(UINT id);
        bool HandleNetworkCommand(UINT id, UINT code);
        bool HandleNetworkNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result);
        void RefreshNetworkInventory(bool preserveSelection);
        void ApplyNetworkFilterToList(bool preserveSelection);
        void RefreshNetworkActionState();
        int SelectedNetworkIndex() const;
        bool TerminateSelectedNetworkProcess(std::wstring &errorText);
        bool CloseSelectedNetworkConnection(std::wstring &errorText);
        bool HandleHardwareCommand(UINT id, UINT code);
        bool HandleHardwareNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result);
        void RefreshHardwareInventory(bool preserveSelection);
        void ApplyHardwareFilterToList(bool preserveSelection);
        void RefreshHardwareActionState();
        int SelectedHardwareIndex() const;
        bool SetHardwareDeviceEnabled(size_t index, bool enable, std::wstring &errorText);
        bool HandleServicesCommand(UINT id, UINT code);
        bool HandleServicesNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result);
        void RefreshServicesInventory(bool preserveSelection);
        void ApplyServicesFilterToList(bool preserveSelection);
        void RefreshServicesActionState();
        int SelectedServiceIndex() const;
        bool HandleStartupAppsCommand(UINT id, UINT code);
        bool HandleStartupAppsNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result);
        void RefreshStartupAppsInventory(bool preserveSelection);
        void RefreshStartupAppsUserTargets();
        void ApplyStartupAppsFilterToList(bool preserveSelection);
        void RefreshStartupAppsActionState();
        int SelectedStartupAppIndex() const;
        bool HandleUsersCommand(UINT id, UINT code);
        bool HandleUsersNotify(const NMHDR *hdr, LPARAM lParam, LRESULT &result);
        void RefreshUsersInventory(bool preserveSelection);
        void ApplyUsersFilterToList(bool preserveSelection);
        void RefreshUsersProcessTargets();
        void ApplyUsersProcessFilterToList(bool preserveSelection);
        void RefreshUsersActionState();
        int SelectedUserIndex() const;
        int SelectedUsersProcessIndex() const;
        const NetworkInterfacePerf *GetActiveNetworkInterface() const;
        const GpuPerf *GetActiveGpu() const;
        void NormalizeDynamicPerformanceSelection();
        void UpdatePerformanceMetrics();
        void UpdateNetworkAdapterInfo();
        void RefreshPerformancePanel();
        void DrawPerformanceGraph(const DRAWITEMSTRUCT *draw) const;
        void DrawCoreGrid(const DRAWITEMSTRUCT *draw) const;
        std::wstring BuildPerformanceDetailsText() const;
        static void PushHistory(std::deque<double> &history, double value, size_t maxSamples = 90);
        double QueryGpuUsagePercent();
        static double CalculateMemoryAxisTopGb(double totalMemoryGb);

        struct NetworkInterfacePerf
        {
            std::wstring key;
            std::wstring adapterName;
            std::wstring ipv4 = L"N/A";
            std::wstring ipv6 = L"N/A";
            ULONG ifType = 0;
            ULONG ifIndex = 0;
            std::uint64_t previousInBytes = 0;
            std::uint64_t previousOutBytes = 0;
            bool hasPreviousSample = false;

            double uploadMbps = 0.0;
            double downloadMbps = 0.0;
            double uploadScaleMbps = 6.0;
            double downloadScaleMbps = 6.0;
            std::deque<double> uploadHistory;
            std::deque<double> downloadHistory;
        };

        struct GpuPerf
        {
            LUID luid{};
            std::wstring name;
            std::wstring location;

            double utilizationPercent = 0.0;
            double engine3dPercent = 0.0;
            double engineCopyPercent = 0.0;
            double engineDecodePercent = 0.0;
            double engineEncodePercent = 0.0;

            double dedicatedGb = 0.0;
            double sharedGb = 0.0;
            double dedicatedUsedGb = 0.0;
            double sharedUsedGb = 0.0;

            std::deque<double> utilizationHistory;
            std::deque<double> dedicatedHistory;
            std::deque<double> sharedHistory;
        };

        struct PerformanceSubviewBinding
        {
            UINT commandId = 0;
            PerformanceView view = PerformanceView::All;
            size_t sourceIndex = 0;
            std::wstring label;
        };

        struct HardwareDeviceEntry
        {
            std::wstring instanceId;
            std::wstring name;
            std::wstring className;
            std::wstring location;
            std::wstring statusText;
            bool enabled = false;
            bool disableCapable = false;
        };

        struct NetworkConnectionEntry
        {
            std::wstring rowKey;
            std::wstring protocol;
            std::wstring addressFamily;
            std::wstring localEndpoint;
            std::wstring remoteEndpoint;
            std::wstring stateText;
            std::wstring processName;
            DWORD pid = 0;
            bool isTcp = false;
            bool isListening = false;
            bool isEstablished = false;
            bool canClose = false;
            bool closeSupported = false;

            DWORD tcpLocalAddrNetwork = 0;
            DWORD tcpRemoteAddrNetwork = 0;
            DWORD tcpLocalPortNetwork = 0;
            DWORD tcpRemotePortNetwork = 0;
        };

        void HandleSnapshotUpdate();
        void RefreshProcessView();

        void ApplyFilterAndSort();
        std::uint32_t GetSelectedPid() const;

        void ShowProcessContextMenu(POINT screenPoint);
        void ExecuteProcessCommand(UINT commandId);
        bool HandleQuickToolsCommand(UINT id);

        void ShowStatusText(const std::wstring &text);

        std::wstring CellText(size_t row, int subItem) const;
        static std::wstring FormatBytes(std::uint64_t bytes);
        static std::wstring FormatCpu(double cpu);
        static std::wstring PriorityClassToText(std::uint32_t priorityClass);

        HINSTANCE instance_ = nullptr;
        HWND hwnd_ = nullptr;

        SidebarNavigation sidebarNavigationComponent_{};
        SidebarRetractable sidebarRetractableComponent_{};
        ProcessListView processListViewComponent_{};
        PerformancePanel performancePanelComponent_{};
        NetworkPanel networkPanelComponent_{};
        HardwarePanel hardwarePanelComponent_{};
        ServicesPanel servicesPanelComponent_{};
        StartupAppsPanel startupAppsPanelComponent_{};
        UsersPanel usersPanelComponent_{};
        QuickToolsPanel quickToolsPanelComponent_{};
        StatusBar statusBarComponent_{};

        HWND sidebar_ = nullptr;
        HWND sidebarTitle_ = nullptr;
        HWND sidebarToggle_ = nullptr;
        HWND navProcesses_ = nullptr;
        HWND navPerformance_ = nullptr;
        HWND navNetwork_ = nullptr;
        HWND navHardware_ = nullptr;
        HWND navServices_ = nullptr;
        HWND navStartupApps_ = nullptr;
        HWND navUsers_ = nullptr;
        HWND navQuickTools_ = nullptr;

        HWND sectionTitle_ = nullptr;
        HWND filterLabel_ = nullptr;
        HWND filterEdit_ = nullptr;
        HWND processList_ = nullptr;
        HWND performancePlaceholder_ = nullptr;
        HWND perfNavPanel_ = nullptr;
        HWND perfNavAll_ = nullptr;
        HWND perfNavCpu_ = nullptr;
        HWND perfNavMemory_ = nullptr;
        HWND perfNavDisk_ = nullptr;
        HWND perfNavWifi_ = nullptr;
        HWND perfNavEthernet_ = nullptr;
        HWND perfNavGpu_ = nullptr;
        HWND perfCpuModeSingle_ = nullptr;
        HWND perfCpuModePerCore_ = nullptr;
        std::vector<HWND> perfNavExtraButtons_;
        HWND perfCoreGrid_ = nullptr;
        HWND perfGraphCpu_ = nullptr;
        HWND perfGraphMemory_ = nullptr;
        HWND perfGraphGpu_ = nullptr;
        HWND perfGraphUpload_ = nullptr;
        HWND perfGraphDownload_ = nullptr;
        HWND perfDetails_ = nullptr;
        HWND networkTitle_ = nullptr;
        HWND networkHint_ = nullptr;
        HWND networkSearchLabel_ = nullptr;
        HWND networkSearchEdit_ = nullptr;
        HWND networkModeLabel_ = nullptr;
        HWND networkModeCombo_ = nullptr;
        HWND networkRefreshButton_ = nullptr;
        HWND networkTerminateButton_ = nullptr;
        HWND networkCloseButton_ = nullptr;
        HWND networkList_ = nullptr;
        HWND networkStatus_ = nullptr;
        HWND hardwarePlaceholder_ = nullptr;
        HWND hardwareHint_ = nullptr;
        HWND hardwareSearchLabel_ = nullptr;
        HWND hardwareSearchEdit_ = nullptr;
        HWND hardwareList_ = nullptr;
        HWND hardwareRefreshButton_ = nullptr;
        HWND hardwareToggleButton_ = nullptr;
        HWND hardwareStatus_ = nullptr;
        HWND servicesTitle_ = nullptr;
        HWND servicesHint_ = nullptr;
        HWND servicesSearchLabel_ = nullptr;
        HWND servicesSearchEdit_ = nullptr;
        HWND servicesModeLabel_ = nullptr;
        HWND servicesModeCombo_ = nullptr;
        HWND servicesRefreshButton_ = nullptr;
        HWND servicesStartButton_ = nullptr;
        HWND servicesStopButton_ = nullptr;
        HWND servicesRestartButton_ = nullptr;
        HWND servicesOpenConsoleButton_ = nullptr;
        HWND servicesList_ = nullptr;
        HWND servicesStatus_ = nullptr;
        HWND startupAppsTitle_ = nullptr;
        HWND startupAppsHint_ = nullptr;
        HWND startupAppsSearchLabel_ = nullptr;
        HWND startupAppsSearchEdit_ = nullptr;
        HWND startupAppsUserLabel_ = nullptr;
        HWND startupAppsUserCombo_ = nullptr;
        HWND startupAppsModeLabel_ = nullptr;
        HWND startupAppsModeCombo_ = nullptr;
        HWND startupAppsRefreshButton_ = nullptr;
        HWND startupAppsEnableButton_ = nullptr;
        HWND startupAppsDisableButton_ = nullptr;
        HWND startupAppsOpenLocationButton_ = nullptr;
        HWND startupAppsList_ = nullptr;
        HWND startupAppsStatus_ = nullptr;
        HWND usersTitle_ = nullptr;
        HWND usersHint_ = nullptr;
        HWND usersSearchLabel_ = nullptr;
        HWND usersSearchEdit_ = nullptr;
        HWND usersModeLabel_ = nullptr;
        HWND usersModeCombo_ = nullptr;
        HWND usersRefreshButton_ = nullptr;
        HWND usersLogoffButton_ = nullptr;
        HWND usersDisconnectButton_ = nullptr;
        HWND usersProcessTitle_ = nullptr;
        HWND usersProcessUserLabel_ = nullptr;
        HWND usersProcessUserCombo_ = nullptr;
        HWND usersList_ = nullptr;
        HWND usersProcessList_ = nullptr;
        HWND usersStatus_ = nullptr;
        HWND quickToolsTitle_ = nullptr;
        HWND quickToolsHint_ = nullptr;
        HWND quickPortLabel_ = nullptr;
        HWND quickPortEdit_ = nullptr;
        HWND quickPortKillOneButton_ = nullptr;
        HWND quickProcessLabel_ = nullptr;
        HWND quickProcessEdit_ = nullptr;
        HWND quickProcessKillOneButton_ = nullptr;
        HWND quickDeleteLabel_ = nullptr;
        HWND quickDeletePathEdit_ = nullptr;
        HWND quickBrowseFileButton_ = nullptr;
        HWND quickBrowseFolderButton_ = nullptr;
        HWND quickKillPortButton_ = nullptr;
        HWND quickKillPatternButton_ = nullptr;
        HWND quickKillSmartDeleteButton_ = nullptr;
        HWND statusText_ = nullptr;

        HBRUSH backgroundBrush_ = nullptr;
        HBRUSH sidebarBrush_ = nullptr;
        HBRUSH cardBrush_ = nullptr;

        HFONT uiFont_ = nullptr;
        HFONT uiBoldFont_ = nullptr;
        HFONT uiTitleFont_ = nullptr;

        core::engine::MonitorEngine engine_;
        core::model::RuntimeState runtimeState_{};
        core::model::SystemSnapshot snapshot_{};

        std::wstring filterText_;
        std::wstring networkFilterText_;
        std::vector<size_t> visibleRows_;
        int lastVisibleCount_ = -1;
        std::wstring hardwareFilterText_;
        std::wstring servicesFilterText_;
        std::wstring startupAppsFilterText_;
        std::wstring usersFilterText_;
        std::wstring activeUsersProcessTarget_ = L"*";
        std::wstring activeStartupAppsTargetId_ = L"current-user";
        std::vector<size_t> networkVisibleRows_;
        std::vector<size_t> hardwareVisibleRows_;
        std::vector<size_t> servicesVisibleRows_;
        std::vector<size_t> startupAppsVisibleRows_;
        std::vector<size_t> usersVisibleRows_;
        std::vector<size_t> usersProcessVisibleRows_;

        std::vector<double> performanceCoreUsage_;
        std::vector<std::deque<double>> performanceCoreHistory_;
        std::vector<std::uint64_t> previousCpuIdle_;
        std::vector<std::uint64_t> previousCpuTotal_;
        bool cpuSamplingReady_ = false;

        std::uint64_t previousNetworkTickMs_ = 0;
        bool networkSamplingReady_ = false;
        std::vector<NetworkInterfacePerf> networkInterfaces_;
        std::vector<NetworkConnectionEntry> networkConnections_;
        std::vector<GpuPerf> gpuDevices_;
        std::vector<HardwareDeviceEntry> hardwareDevices_;
        std::vector<system::services::ServiceInfo> services_;
        std::vector<system::startup::StartupAppEntry> startupApps_;
        std::vector<system::startup::StartupUserTarget> startupAppsUserTargets_;
        std::vector<system::users::UserSessionInfo> usersSessions_;
        std::vector<system::users::UserProcessInfo> usersProcesses_;
        std::vector<PerformanceSubviewBinding> perfDynamicNavBindings_;
        std::unordered_map<DWORD, std::pair<std::uint64_t, std::uint64_t>> usersPreviousIoTotals_;
        std::vector<std::pair<std::wstring, std::wstring>> usersProcessTargets_;
        std::unordered_map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> usersProcessPreviousIoTotals_;
        size_t perfStaticDynamicButtonCount_ = 0;
        std::wstring perfDynamicNavSignature_;
        size_t activeNetworkInterfaceIndex_ = 0;
        size_t activeGpuIndex_ = 0;
        std::uint64_t lastNetworkRefreshTickMs_ = 0;
        std::uint64_t lastHardwareRefreshTickMs_ = 0;
        std::uint64_t lastServicesRefreshTickMs_ = 0;
        std::uint64_t lastStartupAppsRefreshTickMs_ = 0;
        std::uint64_t lastUsersRefreshTickMs_ = 0;
        std::uint64_t usersIoSampleTickMs_ = 0;
        std::uint64_t usersProcessIoSampleTickMs_ = 0;

        double totalCpuPercent_ = 0.0;
        double memoryPercent_ = 0.0;
        double memoryUsedGb_ = 0.0;
        double memoryTotalGb_ = 0.0;
        double memoryAxisTopGb_ = 1.0;
        double memoryAvailableGb_ = 0.0;
        double memoryCommittedGb_ = 0.0;
        double memoryCommitLimitGb_ = 0.0;
        double gpuPercent_ = 0.0;
        double gpu3dPercent_ = 0.0;
        double gpuCopyPercent_ = 0.0;
        double gpuDecodePercent_ = 0.0;
        double gpuEncodePercent_ = 0.0;
        double uploadMbps_ = 0.0;
        double downloadMbps_ = 0.0;
        double wifiUploadMbps_ = 0.0;
        double wifiDownloadMbps_ = 0.0;
        double ethernetUploadMbps_ = 0.0;
        double ethernetDownloadMbps_ = 0.0;
        double diskReadMBps_ = 0.0;
        double diskWriteMBps_ = 0.0;
        double diskActivePercent_ = 0.0;
        double diskAvgResponseMs_ = 0.0;
        double uploadScaleMbps_ = 6.0;
        double downloadScaleMbps_ = 6.0;
        double wifiUploadScaleMbps_ = 6.0;
        double wifiDownloadScaleMbps_ = 6.0;
        double ethernetUploadScaleMbps_ = 6.0;
        double ethernetDownloadScaleMbps_ = 6.0;
        double diskReadScaleMBps_ = 10.0;
        double diskWriteScaleMBps_ = 10.0;
        double gpuDedicatedGb_ = 0.0;
        double gpuSharedGb_ = 0.0;
        double gpuDedicatedUsedGb_ = 0.0;
        double gpuSharedUsedGb_ = 0.0;
        bool gpuCounterReady_ = false;
        bool diskCounterReady_ = false;

        std::wstring wifiAdapterName_;
        std::wstring wifiIpv4_;
        std::wstring wifiIpv6_;
        std::wstring ethernetAdapterName_;
        std::wstring ethernetIpv4_;
        std::wstring ethernetIpv6_;
        std::wstring gpuAdapterName_;
        std::wstring gpuAdaptersSummary_;
        std::wstring gpuLocation_;
        std::wstring diskType_;
        std::wstring systemDrive_;

        std::deque<double> cpuHistory_;
        std::deque<double> memoryHistory_;
        std::deque<double> memoryUtilizationHistory_;
        std::deque<double> memoryHeadroomHistory_;
        std::deque<double> memoryAvailableHistory_;
        std::deque<double> memoryCommittedHistory_;
        std::deque<double> gpuHistory_;
        std::deque<double> gpu3dHistory_;
        std::deque<double> gpuCopyHistory_;
        std::deque<double> gpuDecodeHistory_;
        std::deque<double> gpuEncodeHistory_;
        std::deque<double> gpuDedicatedHistory_;
        std::deque<double> gpuSharedHistory_;
        std::deque<double> uploadHistory_;
        std::deque<double> downloadHistory_;
        std::deque<double> wifiUploadHistory_;
        std::deque<double> wifiDownloadHistory_;
        std::deque<double> ethernetUploadHistory_;
        std::deque<double> ethernetDownloadHistory_;
        std::deque<double> diskReadHistory_;
        std::deque<double> diskWriteHistory_;
        std::deque<double> diskActiveHistory_;

        PDH_HQUERY gpuQuery_ = nullptr;
        PDH_HCOUNTER gpuCounter_ = nullptr;
        PDH_HQUERY diskQuery_ = nullptr;
        PDH_HCOUNTER diskReadCounter_ = nullptr;
        PDH_HCOUNTER diskWriteCounter_ = nullptr;
        PDH_HCOUNTER diskActiveCounter_ = nullptr;
        PDH_HCOUNTER diskLatencyCounter_ = nullptr;

        SortColumn sortColumn_ = SortColumn::Cpu;
        bool sortAscending_ = false;
        Section activeSection_ = Section::QuickKillTools;
        PerformanceView activePerformanceView_ = PerformanceView::All;
        CpuGraphMode cpuGraphMode_ = CpuGraphMode::Single;
        NetworkFilterMode activeNetworkFilterMode_ = NetworkFilterMode::All;
        ServiceFilterMode activeServiceFilterMode_ = ServiceFilterMode::All;
        StartupAppsFilterMode activeStartupAppsFilterMode_ = StartupAppsFilterMode::All;
        UsersFilterMode activeUsersFilterMode_ = UsersFilterMode::All;
        int perfAllScrollOffset_ = 0;
        int perfAllContentHeight_ = 0;
        int perfCpuCoreScrollOffset_ = 0;
        int perfCpuCoreContentHeight_ = 0;

        std::wstring quickDeleteTargetPath_;
        bool quickDeleteTargetIsDirectory_ = false;

        std::uint32_t contextPid_ = 0;
    };

} // namespace utm::ui
