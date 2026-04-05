#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace utm::core::model
{

    struct ProcessSnapshot
    {
        std::uint32_t pid = 0;
        std::uint32_t parentPid = 0;
        std::wstring imageName;

        double cpuPercent = 0.0;
        std::uint64_t workingSetBytes = 0;
        std::uint64_t privateBytes = 0;
        std::uint64_t readBytes = 0;
        std::uint64_t writeBytes = 0;

        std::uint32_t threadCount = 0;
        std::uint32_t handleCount = 0;
        std::int32_t basePriority = 0;
        std::uint32_t priorityClass = 0;

        std::uint64_t createTime100ns = 0;
    };

    struct RuntimeState
    {
        bool isElevated = false;
        bool seDebugEnabled = false;
    };

    struct SystemSnapshot
    {
        std::uint64_t timestampMs = 0;
        RuntimeState runtime;
        std::vector<ProcessSnapshot> processes;
    };

} // namespace utm::core::model
