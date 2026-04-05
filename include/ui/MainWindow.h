#pragma once

#include "core/engine/MonitorEngine.h"
#include "core/model/ProcessSnapshot.h"

#include <windows.h>
#include <pdh.h>

#include <deque>
#include <string>
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

        static LRESULT CALLBACK WndProcSetup(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        bool RegisterWindowClass();
        bool CreateChildControls();
        void LayoutControls();
        void ApplySectionVisibility();
        void SetActiveSection(Section section);
        void UpdateSidebarSelection();
        std::wstring SectionTitleText() const;
        void UpdatePerformanceMetrics();
        void RefreshPerformancePanel();
        void DrawPerformanceGraph(const DRAWITEMSTRUCT *draw) const;
        void DrawCoreGrid(const DRAWITEMSTRUCT *draw) const;
        static void PushHistory(std::deque<double> &history, double value, size_t maxSamples = 90);
        double QueryGpuUsagePercent();

        void HandleSnapshotUpdate();
        void RefreshProcessView();

        void ApplyFilterAndSort();
        std::uint32_t GetSelectedPid() const;

        void ShowProcessContextMenu(POINT screenPoint);
        void ExecuteProcessCommand(UINT commandId);

        void ShowStatusText(const std::wstring &text);

        std::wstring CellText(size_t row, int subItem) const;
        static std::wstring FormatBytes(std::uint64_t bytes);
        static std::wstring FormatCpu(double cpu);
        static std::wstring PriorityClassToText(std::uint32_t priorityClass);

        HINSTANCE instance_ = nullptr;
        HWND hwnd_ = nullptr;

        HWND sidebar_ = nullptr;
        HWND sidebarTitle_ = nullptr;
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
        HWND perfCoreGrid_ = nullptr;
        HWND perfGraphCpu_ = nullptr;
        HWND perfGraphMemory_ = nullptr;
        HWND perfGraphGpu_ = nullptr;
        HWND perfGraphUpload_ = nullptr;
        HWND perfGraphDownload_ = nullptr;
        HWND networkPlaceholder_ = nullptr;
        HWND hardwarePlaceholder_ = nullptr;
        HWND servicesPlaceholder_ = nullptr;
        HWND startupAppsPlaceholder_ = nullptr;
        HWND usersPlaceholder_ = nullptr;
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
        std::vector<size_t> visibleRows_;
        int lastVisibleCount_ = -1;

        std::vector<double> performanceCoreUsage_;
        std::vector<std::deque<double>> performanceCoreHistory_;
        std::vector<std::uint64_t> previousCpuIdle_;
        std::vector<std::uint64_t> previousCpuTotal_;
        bool cpuSamplingReady_ = false;

        std::uint64_t previousNetworkIn_ = 0;
        std::uint64_t previousNetworkOut_ = 0;
        std::uint64_t previousNetworkTickMs_ = 0;
        bool networkSamplingReady_ = false;

        double totalCpuPercent_ = 0.0;
        double memoryPercent_ = 0.0;
        double memoryUsedGb_ = 0.0;
        double memoryTotalGb_ = 0.0;
        double gpuPercent_ = 0.0;
        double uploadMbps_ = 0.0;
        double downloadMbps_ = 0.0;
        double uploadScaleMbps_ = 6.0;
        double downloadScaleMbps_ = 6.0;
        bool gpuCounterReady_ = false;

        std::deque<double> cpuHistory_;
        std::deque<double> memoryHistory_;
        std::deque<double> gpuHistory_;
        std::deque<double> uploadHistory_;
        std::deque<double> downloadHistory_;

        PDH_HQUERY gpuQuery_ = nullptr;
        PDH_HCOUNTER gpuCounter_ = nullptr;

        SortColumn sortColumn_ = SortColumn::Cpu;
        bool sortAscending_ = false;
        Section activeSection_ = Section::QuickKillTools;

        std::wstring quickDeleteTargetPath_;
        bool quickDeleteTargetIsDirectory_ = false;

        std::uint32_t contextPid_ = 0;
    };

} // namespace utm::ui
