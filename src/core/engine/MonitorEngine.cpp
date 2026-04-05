#include "core/engine/MonitorEngine.h"

#include "util/logging/Logger.h"

#include <algorithm>

namespace utm::core::engine
{

    namespace
    {

        std::uint64_t BuildCpuKey(std::uint32_t pid, std::uint64_t createTime100ns)
        {
            return (static_cast<std::uint64_t>(pid) << 32) ^ createTime100ns;
        }

        std::wstring ToLowerCopy(const std::wstring &value)
        {
            std::wstring out = value;
            std::transform(out.begin(), out.end(), out.begin(), towlower);
            return out;
        }

    } // namespace

    MonitorEngine::MonitorEngine(std::chrono::milliseconds refreshInterval)
        : refreshInterval_(refreshInterval)
    {
    }

    MonitorEngine::~MonitorEngine()
    {
        Stop();
    }

    bool MonitorEngine::Start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true))
        {
            return false;
        }

        worker_ = std::thread(&MonitorEngine::RunLoop, this);
        return true;
    }

    void MonitorEngine::Stop()
    {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false))
        {
            return;
        }

        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void MonitorEngine::SetRuntimeState(const model::RuntimeState &runtime)
    {
        std::scoped_lock lock(snapshotMutex_);
        snapshot_.runtime = runtime;
    }

    void MonitorEngine::SetUiNotification(HWND hwnd, UINT messageId)
    {
        notifyWindow_ = hwnd;
        notifyMessageId_ = messageId;
    }

    void MonitorEngine::SetSnapshotCallback(std::function<void()> callback)
    {
        snapshotCallback_ = std::move(callback);
    }

    model::SystemSnapshot MonitorEngine::GetSnapshot() const
    {
        std::scoped_lock lock(snapshotMutex_);
        return snapshot_;
    }

    std::uint64_t MonitorEngine::GetMonotonicMs()
    {
        return static_cast<std::uint64_t>(GetTickCount64());
    }

    void MonitorEngine::PublishSnapshot(std::vector<model::ProcessSnapshot> &&processes)
    {
        {
            std::scoped_lock lock(snapshotMutex_);
            snapshot_.timestampMs = GetMonotonicMs();
            snapshot_.processes = std::move(processes);
        }

        if (snapshotCallback_)
        {
            snapshotCallback_();
        }

        if (notifyWindow_ && notifyMessageId_ != 0)
        {
            PostMessageW(notifyWindow_, notifyMessageId_, 0, 0);
        }
    }

    void MonitorEngine::RunLoop()
    {
        constexpr double k100nsToSeconds = 10000000.0;
        const auto cpuCount = std::max(1u, std::thread::hardware_concurrency());

        util::logging::Logger::Instance().Write(util::logging::LogLevel::Info, L"Monitor engine started.");

        while (running_.load())
        {
            const auto cycleStart = std::chrono::steady_clock::now();

            auto raw = enumerator_.Enumerate();
            std::vector<model::ProcessSnapshot> output;
            output.reserve(raw.size());

            std::unordered_map<std::uint64_t, CpuSample> nextSamples;
            nextSamples.reserve(raw.size());

            const double elapsedSeconds = std::max(0.001, refreshInterval_.count() / 1000.0);

            for (const auto &r : raw)
            {
                model::ProcessSnapshot item;
                item.pid = r.pid;
                item.parentPid = r.parentPid;
                item.imageName = r.imageName;
                item.threadCount = r.threadCount;
                item.handleCount = r.handleCount;
                item.basePriority = r.basePriority;
                item.workingSetBytes = r.workingSetBytes;
                item.privateBytes = r.privateBytes;
                item.readBytes = r.readBytes;
                item.writeBytes = r.writeBytes;
                item.createTime100ns = r.createTime100ns;

                const std::uint64_t cpuNow = r.kernelTime100ns + r.userTime100ns;
                const std::uint64_t key = BuildCpuKey(r.pid, r.createTime100ns);

                double cpu = 0.0;
                if (const auto it = cpuSamples_.find(key); it != cpuSamples_.end())
                {
                    const auto delta100ns = cpuNow > it->second.totalCpu100ns ? cpuNow - it->second.totalCpu100ns : 0;
                    cpu = (static_cast<double>(delta100ns) / k100nsToSeconds) / (elapsedSeconds * cpuCount) * 100.0;
                    if (cpu < 0.0)
                    {
                        cpu = 0.0;
                    }
                    if (cpu > 100.0)
                    {
                        cpu = 100.0;
                    }
                }

                item.cpuPercent = cpu;

                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.pid);
                if (process)
                {
                    item.priorityClass = GetPriorityClass(process);
                    CloseHandle(process);
                }

                nextSamples.emplace(key, CpuSample{cpuNow});
                output.push_back(std::move(item));
            }

            cpuSamples_.swap(nextSamples);
            PublishSnapshot(std::move(output));

            const auto cycleEnd = std::chrono::steady_clock::now();
            const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(cycleEnd - cycleStart);
            if (spent < refreshInterval_)
            {
                std::this_thread::sleep_for(refreshInterval_ - spent);
            }
        }

        util::logging::Logger::Instance().Write(util::logging::LogLevel::Info, L"Monitor engine stopped.");
    }

} // namespace utm::core::engine
