#include "ui/PerformancePanel.h"

#include "ui/MainWindow.h"

#include <ipifcons.h>
#include <windowsx.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace
{
    constexpr UINT kIdPerfNavAll = 1040;
    constexpr UINT kIdPerfNavCpu = 1041;
    constexpr UINT kIdPerfNavMemory = 1042;
    constexpr UINT kIdPerfNavDisk = 1043;
    constexpr UINT kIdPerfNavWifi = 1044;
    constexpr UINT kIdPerfNavEthernet = 1045;
    constexpr UINT kIdPerfNavGpu = 1046;
    constexpr UINT kIdPerfNavDynamicBase = 1100;
    constexpr UINT kIdPerfNavDynamicMax = 1199;
    constexpr UINT kIdPerfCpuModeSingle = 1048;
    constexpr UINT kIdPerfCpuModePerCore = 1049;

    constexpr UINT kIdPerfCoreGrid = 43000;
    constexpr UINT kIdPerfGraphCpu = 43001;
    constexpr UINT kIdPerfGraphMemory = 43002;
    constexpr UINT kIdPerfGraphGpu = 43003;
    constexpr UINT kIdPerfGraphUpload = 43004;
    constexpr UINT kIdPerfGraphDownload = 43005;
}

namespace utm::ui
{

    void PerformancePanel::Attach(MainWindow *owner)
    {
        owner_ = owner;
    }

    bool PerformancePanel::HandleCommand(UINT id)
    {
        if (!owner_)
        {
            return false;
        }

        if (id == kIdPerfNavCpu)
        {
            owner_->SetActivePerformanceView(MainWindow::PerformanceView::Cpu);
            return true;
        }

        if (id == kIdPerfCpuModeSingle)
        {
            owner_->SetCpuGraphMode(MainWindow::CpuGraphMode::Single);
            return true;
        }

        if (id == kIdPerfCpuModePerCore)
        {
            owner_->SetCpuGraphMode(MainWindow::CpuGraphMode::PerCore);
            return true;
        }

        if (id == kIdPerfNavAll)
        {
            owner_->SetActivePerformanceView(MainWindow::PerformanceView::All);
            return true;
        }

        if (id == kIdPerfNavMemory)
        {
            owner_->SetActivePerformanceView(MainWindow::PerformanceView::Memory);
            return true;
        }

        if (id == kIdPerfNavDisk)
        {
            owner_->SetActivePerformanceView(MainWindow::PerformanceView::Disk);
            return true;
        }

        if (id == kIdPerfNavWifi || id == kIdPerfNavEthernet || id == kIdPerfNavGpu ||
            (id >= kIdPerfNavDynamicBase && id <= kIdPerfNavDynamicMax))
        {
            return owner_->HandleDynamicPerformanceSubviewCommand(id);
        }

        return false;
    }

    void MainWindow::SetActivePerformanceView(PerformanceView view)
    {
        if (activePerformanceView_ == view)
        {
            return;
        }

        activePerformanceView_ = view;
        if (activePerformanceView_ == PerformanceView::All ||
            activePerformanceView_ == PerformanceView::Gpu)
        {
            perfAllScrollOffset_ = 0;
        }
        if (activePerformanceView_ != PerformanceView::Cpu)
        {
            perfCpuCoreScrollOffset_ = 0;
            perfCpuCoreContentHeight_ = 0;
        }
        UpdatePerformanceSubviewSelection();

        if (activeSection_ == Section::Performance)
        {
            LayoutControls();
            RefreshPerformancePanel();
        }
    }

    void MainWindow::SetCpuGraphMode(CpuGraphMode mode)
    {
        if (cpuGraphMode_ == mode)
        {
            return;
        }

        cpuGraphMode_ = mode;
        if (cpuGraphMode_ == CpuGraphMode::Single)
        {
            perfCpuCoreScrollOffset_ = 0;
            perfCpuCoreContentHeight_ = 0;
        }

        UpdatePerformanceSubviewSelection();

        if (activeSection_ == Section::Performance && activePerformanceView_ == PerformanceView::Cpu)
        {
            LayoutControls();
            RefreshPerformancePanel();
        }
    }

    void MainWindow::UpdatePerformanceSubviewSelection()
    {
        const auto isDynamicBindingSelected = [&](UINT commandId)
        {
            for (const auto &binding : perfDynamicNavBindings_)
            {
                if (binding.commandId != commandId)
                {
                    continue;
                }

                if (binding.view == PerformanceView::Gpu)
                {
                    return activePerformanceView_ == PerformanceView::Gpu && activeGpuIndex_ == binding.sourceIndex;
                }

                return activePerformanceView_ == binding.view && activeNetworkInterfaceIndex_ == binding.sourceIndex;
            }

            return false;
        };

        const auto setChecked = [](HWND button, bool checked)
        {
            if (button)
            {
                SendMessageW(button, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
            }
        };

        setChecked(perfNavAll_, activePerformanceView_ == PerformanceView::All);
        setChecked(perfNavCpu_, activePerformanceView_ == PerformanceView::Cpu);
        setChecked(perfNavMemory_, activePerformanceView_ == PerformanceView::Memory);
        setChecked(perfNavDisk_, activePerformanceView_ == PerformanceView::Disk);
        setChecked(perfNavWifi_, isDynamicBindingSelected(kIdPerfNavWifi));
        setChecked(perfNavEthernet_, isDynamicBindingSelected(kIdPerfNavEthernet));
        setChecked(perfNavGpu_, isDynamicBindingSelected(kIdPerfNavGpu));
        setChecked(perfCpuModeSingle_, cpuGraphMode_ == CpuGraphMode::Single);
        setChecked(perfCpuModePerCore_, cpuGraphMode_ == CpuGraphMode::PerCore);

        for (size_t i = 0; i < perfNavExtraButtons_.size(); ++i)
        {
            const UINT commandId = static_cast<UINT>(GetDlgCtrlID(perfNavExtraButtons_[i]));
            setChecked(perfNavExtraButtons_[i], isDynamicBindingSelected(commandId));
        }
    }

    const MainWindow::NetworkInterfacePerf *MainWindow::GetActiveNetworkInterface() const
    {
        const bool wifiView = activePerformanceView_ == PerformanceView::Wifi;
        const bool ethernetView = activePerformanceView_ == PerformanceView::Ethernet;
        if (!wifiView && !ethernetView)
        {
            return nullptr;
        }

        const auto matchesView = [&](const NetworkInterfacePerf &iface)
        {
            return wifiView ? (iface.ifType == IF_TYPE_IEEE80211) : (iface.ifType != IF_TYPE_IEEE80211);
        };

        if (activeNetworkInterfaceIndex_ < networkInterfaces_.size())
        {
            const auto &candidate = networkInterfaces_[activeNetworkInterfaceIndex_];
            if (matchesView(candidate))
            {
                return &candidate;
            }
        }

        for (const auto &iface : networkInterfaces_)
        {
            if (matchesView(iface))
            {
                return &iface;
            }
        }

        return nullptr;
    }

    const MainWindow::GpuPerf *MainWindow::GetActiveGpu() const
    {
        if (gpuDevices_.empty())
        {
            return nullptr;
        }

        if (activeGpuIndex_ < gpuDevices_.size())
        {
            return &gpuDevices_[activeGpuIndex_];
        }

        return &gpuDevices_.front();
    }

    void MainWindow::NormalizeDynamicPerformanceSelection()
    {
        if (activePerformanceView_ == PerformanceView::Wifi || activePerformanceView_ == PerformanceView::Ethernet)
        {
            const auto *iface = GetActiveNetworkInterface();
            if (!iface)
            {
                activePerformanceView_ = PerformanceView::All;
                activeNetworkInterfaceIndex_ = 0;
            }
            else
            {
                activeNetworkInterfaceIndex_ = static_cast<size_t>(iface - networkInterfaces_.data());
            }
        }

        if (activePerformanceView_ == PerformanceView::Gpu)
        {
            const auto *gpu = GetActiveGpu();
            if (!gpu)
            {
                activePerformanceView_ = PerformanceView::All;
                activeGpuIndex_ = 0;
            }
            else
            {
                activeGpuIndex_ = static_cast<size_t>(gpu - gpuDevices_.data());
            }
        }
    }

    bool MainWindow::HandleDynamicPerformanceSubviewCommand(UINT id)
    {
        for (const auto &binding : perfDynamicNavBindings_)
        {
            if (binding.commandId != id)
            {
                continue;
            }

            const bool viewChanged = activePerformanceView_ != binding.view;
            bool indexChanged = false;
            if (binding.view == PerformanceView::Gpu)
            {
                indexChanged = activeGpuIndex_ != binding.sourceIndex;
                activeGpuIndex_ = binding.sourceIndex;
            }
            else
            {
                indexChanged = activeNetworkInterfaceIndex_ != binding.sourceIndex;
                activeNetworkInterfaceIndex_ = binding.sourceIndex;
            }

            if (viewChanged)
            {
                SetActivePerformanceView(binding.view);
            }
            else if (indexChanged)
            {
                NormalizeDynamicPerformanceSelection();
                UpdatePerformanceSubviewSelection();
                if (activeSection_ == Section::Performance)
                {
                    RefreshPerformancePanel();
                }
            }

            return true;
        }

        return false;
    }

    void MainWindow::RebuildPerformanceSubviewNavigation()
    {
        if (!hwnd_ || !perfNavPanel_)
        {
            return;
        }

        std::vector<PerformanceSubviewBinding> desiredBindings;
        size_t wifiIndex = 0;
        size_t ethernetIndex = 0;
        size_t otherIndex = 0;

        for (size_t ifaceIndex = 0; ifaceIndex < networkInterfaces_.size(); ++ifaceIndex)
        {
            const auto &iface = networkInterfaces_[ifaceIndex];

            PerformanceSubviewBinding binding{};
            binding.view = iface.ifType == IF_TYPE_IEEE80211 ? PerformanceView::Wifi : PerformanceView::Ethernet;
            binding.sourceIndex = ifaceIndex;

            std::wstring prefix;
            if (iface.ifType == IF_TYPE_IEEE80211)
            {
                prefix = L"Wi-Fi " + std::to_wstring(wifiIndex++);
            }
            else if (iface.ifType == IF_TYPE_ETHERNET_CSMACD)
            {
                prefix = L"Ethernet " + std::to_wstring(ethernetIndex++);
            }
            else
            {
                prefix = L"Network " + std::to_wstring(otherIndex++);
            }

            if (!iface.adapterName.empty() && iface.adapterName != L"N/A")
            {
                binding.label = prefix + L" | " + iface.adapterName;
            }
            else
            {
                binding.label = prefix;
            }

            desiredBindings.push_back(std::move(binding));
        }

        for (size_t gpuIndex = 0; gpuIndex < gpuDevices_.size(); ++gpuIndex)
        {
            PerformanceSubviewBinding binding{};
            binding.view = PerformanceView::Gpu;
            binding.sourceIndex = gpuIndex;
            binding.label = L"GPU " + std::to_wstring(gpuIndex + 1);
            desiredBindings.push_back(std::move(binding));
        }

        std::wstringstream signatureBuilder;
        signatureBuilder << desiredBindings.size() << L"|";
        for (const auto &binding : desiredBindings)
        {
            signatureBuilder << static_cast<int>(binding.view) << L":" << binding.sourceIndex << L":" << binding.label << L";";
        }

        const std::wstring nextSignature = signatureBuilder.str();
        if (nextSignature == perfDynamicNavSignature_)
        {
            NormalizeDynamicPerformanceSelection();
            UpdatePerformanceSubviewSelection();
            return;
        }

        for (HWND button : perfNavExtraButtons_)
        {
            if (button)
            {
                DestroyWindow(button);
            }
        }
        perfNavExtraButtons_.clear();
        perfDynamicNavBindings_.clear();

        const HWND staticDynamicButtons[3] = {perfNavWifi_, perfNavEthernet_, perfNavGpu_};
        const UINT staticDynamicIds[3] = {
            static_cast<UINT>(kIdPerfNavWifi),
            static_cast<UINT>(kIdPerfNavEthernet),
            static_cast<UINT>(kIdPerfNavGpu)};

        perfStaticDynamicButtonCount_ = (std::min)(static_cast<size_t>(3), desiredBindings.size());

        for (size_t i = 0; i < 3; ++i)
        {
            if (i < desiredBindings.size())
            {
                SetWindowTextW(staticDynamicButtons[i], desiredBindings[i].label.c_str());

                PerformanceSubviewBinding mapped = desiredBindings[i];
                mapped.commandId = staticDynamicIds[i];
                perfDynamicNavBindings_.push_back(std::move(mapped));
            }
            else
            {
                SetWindowTextW(staticDynamicButtons[i], L"");
            }
        }

        const int maxExtraButtons = kIdPerfNavDynamicMax - kIdPerfNavDynamicBase + 1;
        for (size_t i = 3; i < desiredBindings.size(); ++i)
        {
            const int dynamicOrdinal = static_cast<int>(i - 3);
            if (dynamicOrdinal >= maxExtraButtons)
            {
                break;
            }

            const int commandId = kIdPerfNavDynamicBase + dynamicOrdinal;
            HWND button = CreateWindowExW(
                0,
                L"BUTTON",
                desiredBindings[i].label.c_str(),
                WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE,
                0,
                0,
                0,
                0,
                hwnd_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(commandId)),
                instance_,
                nullptr);

            if (!button)
            {
                continue;
            }

            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(uiBoldFont_), TRUE);
            perfNavExtraButtons_.push_back(button);

            PerformanceSubviewBinding mapped = desiredBindings[i];
            mapped.commandId = static_cast<UINT>(commandId);
            perfDynamicNavBindings_.push_back(std::move(mapped));
        }

        perfDynamicNavSignature_ = nextSignature;
        NormalizeDynamicPerformanceSelection();

        if (activeSection_ == Section::Performance)
        {
            LayoutControls();
        }
        else
        {
            ApplySectionVisibility();
        }

        UpdatePerformanceSubviewSelection();
    }

    bool PerformancePanel::HandleMouseWheel(WPARAM wParam, LPARAM lParam)
    {
        if (!owner_)
        {
            return false;
        }

        const bool perfSection = owner_->activeSection_ == MainWindow::Section::Performance;
        const bool scrollAllOrGpu =
            owner_->activePerformanceView_ == MainWindow::PerformanceView::All ||
            owner_->activePerformanceView_ == MainWindow::PerformanceView::Gpu;
        const bool scrollCpuPerCore =
            owner_->activePerformanceView_ == MainWindow::PerformanceView::Cpu &&
            owner_->cpuGraphMode_ == MainWindow::CpuGraphMode::PerCore;

        if (!(perfSection && owner_->perfCoreGrid_ && (scrollAllOrGpu || scrollCpuPerCore)))
        {
            return false;
        }

        RECT area{};
        GetWindowRect(owner_->perfCoreGrid_, &area);

        POINT mouse{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (!PtInRect(&area, mouse))
        {
            return false;
        }

        const int wheelSteps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        if (wheelSteps != 0)
        {
            RECT client{};
            GetClientRect(owner_->perfCoreGrid_, &client);
            const int viewport = (std::max)(1, static_cast<int>(client.bottom - client.top));
            int *scrollOffset = scrollCpuPerCore ? &owner_->perfCpuCoreScrollOffset_ : &owner_->perfAllScrollOffset_;
            const int contentHeight = scrollCpuPerCore ? owner_->perfCpuCoreContentHeight_ : owner_->perfAllContentHeight_;
            const int maxOffset = (std::max)(0, contentHeight - viewport);
            const int nextOffset = *scrollOffset - wheelSteps * 40;
            *scrollOffset = (std::clamp)(nextOffset, 0, maxOffset);
            InvalidateRect(owner_->perfCoreGrid_, nullptr, FALSE);
        }

        return true;
    }

    bool PerformancePanel::HandleDrawItem(const DRAWITEMSTRUCT *draw, LRESULT &result) const
    {
        if (!owner_ || !draw)
        {
            return false;
        }

        if (draw->CtlID == kIdPerfGraphCpu ||
            draw->CtlID == kIdPerfGraphMemory ||
            draw->CtlID == kIdPerfGraphGpu ||
            draw->CtlID == kIdPerfGraphUpload ||
            draw->CtlID == kIdPerfGraphDownload)
        {
            owner_->DrawPerformanceGraph(draw);
            result = TRUE;
            return true;
        }

        if (draw->CtlID == kIdPerfCoreGrid)
        {
            owner_->DrawCoreGrid(draw);
            result = TRUE;
            return true;
        }

        return false;
    }

    bool PerformancePanel::IsPerformanceButtonControl(HWND control) const
    {
        if (!owner_)
        {
            return false;
        }

        if (control == owner_->perfNavCpu_ ||
            control == owner_->perfNavAll_ ||
            control == owner_->perfNavMemory_ ||
            control == owner_->perfNavDisk_ ||
            control == owner_->perfNavWifi_ ||
            control == owner_->perfNavEthernet_ ||
            control == owner_->perfNavGpu_ ||
            control == owner_->perfCpuModeSingle_ ||
            control == owner_->perfCpuModePerCore_)
        {
            return true;
        }

        for (HWND extraButton : owner_->perfNavExtraButtons_)
        {
            if (control == extraButton)
            {
                return true;
            }
        }

        return false;
    }

} // namespace utm::ui
