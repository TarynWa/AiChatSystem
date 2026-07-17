#include "Timestamp.hpp"
#include <cstdint>


uint64_t Timestamp::getmicro() const
{
    return microSecondsSinceEpoch_;
}

uint64_t Timestamp::getsecond() const
{
    return microSecondsSinceEpoch_ / KMicPerSec;
}

Timestamp::Timestamp(): microSecondsSinceEpoch_(0)
{
    microSecondsSinceEpoch_ = 0;
}

Timestamp::~Timestamp() = default;

std::string Timestamp::toString() const
{
    char buf[32] = {0};
    uint64_t microseconds = getmicro();
    uint64_t seconds = getsecond();
    snprintf(buf, sizeof(buf), "%lu.%06lu", seconds, microseconds % KMicPerSec);
    return buf;
}

std::string Timestamp::toFormattedString(bool showmicro) const
{
    int64_t seconds = getsecond();
    int64_t microseconds = getmicro() % 1000000;

    struct tm tm_time;
#ifdef _WIN32
    // Windows 平台：使用 gmtime_s
    if (gmtime_s(&tm_time, &seconds) != 0) {
        return "Invalid Time";
    }
#else
    // Linux 平台：使用 gmtime_r
    if (gmtime_r(&seconds, &tm_time) == nullptr) {
        return "Invalid Time";
    }
#endif

    char buf[32] = {0};
    if (showmicro) {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%06lld",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
                 (long long)microseconds);
    } else {
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    }

    return std::string(buf);
}

std::string Timestamp::toFormattedFile() const
{
    const int slen = 64;
    char buff[slen] = {};
    time_t sec = (time_t)getsecond();
    time_t mic = getmicro() - sec * KMicPerSec;

    struct tm tm_time;

#ifdef _WIN32
    // Windows 用 localtime_s
    localtime_s(&tm_time, &sec);
#else
    // Linux 用 localtime_r
    localtime_r(&sec, &tm_time);
#endif

    int pos = sprintf(buff, "%04d%02d%02d%02d%02d%02d.%02lu",
        tm_time.tm_year + 1900,
        tm_time.tm_mon + 1,
        tm_time.tm_mday,
        tm_time.tm_hour,
        tm_time.tm_min,
        tm_time.tm_sec,
        mic);

    return std::string(buff);
}

Timestamp Timestamp::Now()
{
#ifdef _WIN32
    // Windows 平台实现
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    int64_t microsec = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    return Timestamp(microsec);
#else
    // Linux 平台实现
    struct timeval tv;
    ::gettimeofday(&tv, nullptr);
    int64_t microsec = static_cast<int64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
    return Timestamp(microsec);
#endif
}

Timestamp Timestamp::Invalid()
{
    return Timestamp();
}
