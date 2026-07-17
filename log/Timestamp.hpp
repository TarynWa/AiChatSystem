#ifndef TIMESTAMP_HPP
#define TIMESTAMP_HPP
#include <string>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include <chrono>
#else
#include <sys/time.h>
#endif
#include <memory>
class Timestamp
{
private:
    uint64_t microSecondsSinceEpoch_ = 0;
    static const uint32_t KMicPerSec = 1000 * 1000;

public:
    Timestamp();
    ~Timestamp();
    uint64_t getmicro() const;
    uint64_t getsecond() const;
    std::string toString() const;
    //"123.32z"
    std::string toFormattedString(bool showmicro = true) const;
    //"2025/10/11 15:29:23"
    //"2025/10/11 15:29:23.32z"
    std::string toFormattedFile() const;
    static Timestamp Now(); // 现在时间
    static Timestamp Invalid();
    Timestamp(uint64_t ms) : microSecondsSinceEpoch_(ms) {
        // impl_->microSecondsSinceEpoch_ = ms;
        // impl_->vm_handle_ = this;
    }
};

#endif // TIMESTAMP_HPP