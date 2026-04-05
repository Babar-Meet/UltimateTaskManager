#pragma once

#include "core/engine/MonitorEngine.h"
#include "core/model/ProcessSnapshot.h"

#include <windows.h>

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
        HWND navQuickTools_ = nullptr;

        HWND sectionTitle_ = nullptr;
        HWND filterLabel_ = nullptr;
        HWND filterEdit_ = nullptr;
        HWND processList_ = nullptr;
        HWND performancePlaceholder_ = nullptr;
        HWND networkPlaceholder_ = nullptr;
        HWND hardwarePlaceholder_ = nullptr;
        HWND quickToolsTitle_ = nullptr;
        HWND quickToolsHint_ = nullptr;
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

        SortColumn sortColumn_ = SortColumn::Cpu;
        bool sortAscending_ = false;
        Section activeSection_ = Section::Processes;

        std::uint32_t contextPid_ = 0;
    };

} // namespace utm::ui
