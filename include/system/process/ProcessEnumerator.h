#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace utm::system::process
{

    struct RawProcessInfo
    {
        std::uint32_t pid = 0;
        std::uint32_t parentPid = 0;
        std::wstring imageName;

        std::uint32_t threadCount = 0;
        std::uint32_t handleCount = 0;
        std::int32_t basePriority = 0;

        std::uint64_t workingSetBytes = 0;
        std::uint64_t privateBytes = 0;
        std::uint64_t readBytes = 0;
        std::uint64_t writeBytes = 0;

        std::uint64_t kernelTime100ns = 0;
        std::uint64_t userTime100ns = 0;
        std::uint64_t createTime100ns = 0;
    };

    class ProcessEnumerator
    {
    public:
        ProcessEnumerator();

        std::vector<RawProcessInfo> Enumerate();
        bool LastEnumerationUsedNtApi() const;

    private:
        std::vector<RawProcessInfo> EnumerateWithNtApi();
        std::vector<RawProcessInfo> EnumerateWithToolHelp();

        bool ntReady_ = false;
        bool lastUsedNt_ = false;
    };

} // namespace utm::system::process
