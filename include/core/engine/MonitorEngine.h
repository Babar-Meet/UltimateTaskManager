#pragma once

#include "core/model/ProcessSnapshot.h"

#include "system/process/ProcessEnumerator.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace utm::core::engine
{

    class MonitorEngine
    {
    public:
        explicit MonitorEngine(std::chrono::milliseconds refreshInterval = std::chrono::milliseconds(1000));
        ~MonitorEngine();

        MonitorEngine(const MonitorEngine &) = delete;
        MonitorEngine &operator=(const MonitorEngine &) = delete;

        bool Start();
        void Stop();

        void SetRuntimeState(const model::RuntimeState &runtime);
        void SetUiNotification(HWND hwnd, UINT messageId);
        void SetSnapshotCallback(std::function<void()> callback);

        model::SystemSnapshot GetSnapshot() const;

    private:
        struct CpuSample
        {
            std::uint64_t totalCpu100ns = 0;
        };

        void RunLoop();
        void PublishSnapshot(std::vector<model::ProcessSnapshot> &&processes);

        static std::uint64_t GetMonotonicMs();

        std::chrono::milliseconds refreshInterval_;
        std::atomic<bool> running_{false};

        mutable std::mutex snapshotMutex_;
        model::SystemSnapshot snapshot_;

        std::thread worker_;
        std::unordered_map<std::uint64_t, CpuSample> cpuSamples_;

        system::process::ProcessEnumerator enumerator_;

        HWND notifyWindow_ = nullptr;
        UINT notifyMessageId_ = 0;

        std::function<void()> snapshotCallback_;
    };

} // namespace utm::core::engine
