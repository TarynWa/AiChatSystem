#include "Timestamp.hpp"
static uint32_t slen = 128;
namespace wangt
{
    void Timestamp::swap(Timestamp &other)
    {
        Timestamp a = *this;
        *this = other;
        other = a;
    }

    string Timestamp::toString() const
    {
        char buff[slen] = {};
        time_t sec = this->getsecond();
        time_t mic = this->getmicro() - sec * KMicPerSec;
        sprintf(buff, "%lu.%luZ", sec, mic);
        return string(buff);
    }

    string Timestamp::toFormattedString(bool showmicto) const
    {
        char buff[slen] = {};
        time_t sec = this->getsecond();
        time_t mic = this->getmicro() - sec * KMicPerSec;
        struct tm tm_time;
        localtime_r(&sec, &tm_time);
        int pos = sprintf(buff, "%04d/%02d/%02d/%02d:%02d:%02d",
                          tm_time.tm_year + 1900,
                          tm_time.tm_mon + 1,
                          tm_time.tm_mday,
                          tm_time.tm_hour,
                          tm_time.tm_min,
                          tm_time.tm_sec);
        if (showmicto)
        {
            sprintf(buff + pos, ".%04luZ", mic);
        }
        return string(buff);
    }

    string Timestamp::toFormattedFile() const
    {
        char buff[slen] = {};
        time_t sec = this->getsecond();
        time_t mic = this->getmicro() - sec * KMicPerSec;
        struct tm tm_time;
        localtime_r(&sec, &tm_time);
        int pos = sprintf(buff, "%04d%02d%02d%02d%02d%02d.%02lu",
                          tm_time.tm_year + 1900,
                          tm_time.tm_mon + 1,
                          tm_time.tm_mday,
                          tm_time.tm_hour,
                          tm_time.tm_min,
                          tm_time.tm_sec,
                          mic);
        return string(buff);
    }

    bool Timestamp::valid() const
    {
        return micr_ > 0;
    }

    uint64_t Timestamp::getmicro() const
    {
        return micr_;
    }

    uint64_t Timestamp::getsecond() const
    {
        return micr_ / KMicPerSec;
    }

    const Timestamp &Timestamp::now()
    {
        *this = wangt::Timestamp::Now();
        return *this;
    }

    Timestamp::operator uint64_t() const
    {
        return micr_;
    }

    Timestamp Timestamp::Now()
    {
        struct timeval tv;
        ::gettimeofday(&tv, nullptr);
        return Timestamp(tv.tv_usec + tv.tv_sec * KMicPerSec);
    }
    Timestamp Timestamp::Invalid()
    {
        return Timestamp(0);
    }
}
