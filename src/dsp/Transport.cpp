#include "Transport.h"

#include <algorithm>

namespace stemmerizer::dsp
{

long long Transport::advance (long long n) noexcept
{
    long long p   = pos.load (std::memory_order_acquire);
    const long long len = totalLen.load (std::memory_order_acquire);
    if (len <= 0) return p;

    p += n;

    if (loopOn.load (std::memory_order_acquire))
    {
        const long long s = lpStart.load (std::memory_order_acquire);
        const long long e = lpEnd.load   (std::memory_order_acquire);
        if (e > s && p >= e)
        {
            const long long span = e - s;
            // Modulo into the loop region (handles huge jumps without an
            // unbounded loop).
            p = s + ((p - s) % span);
        }
    }
    else if (p >= len)
    {
        p = len;
        playing.store (false, std::memory_order_release);
    }

    pos.store (p, std::memory_order_release);
    return p;
}

void Transport::prepare (long long lengthInSamples, int sampleRateHz)
{
    totalLen.store (lengthInSamples, std::memory_order_release);
    sr.store       (sampleRateHz,    std::memory_order_release);

    pos.store (0, std::memory_order_release);
    lpStart.store (0, std::memory_order_release);
    lpEnd.store   (lengthInSamples, std::memory_order_release);
    notifyChanged();
}

void Transport::play()  { playing.store (true,  std::memory_order_release); notifyChanged(); }
void Transport::pause() { playing.store (false, std::memory_order_release); notifyChanged(); }

void Transport::stop()
{
    playing.store (false, std::memory_order_release);
    const long long s = loopOn.load (std::memory_order_acquire) ? lpStart.load() : 0LL;
    pos.store (s, std::memory_order_release);
    notifyChanged();
}

void Transport::seek (long long sample)
{
    const long long len = totalLen.load (std::memory_order_acquire);
    sample = std::max<long long> (0, std::min (sample, len));
    pos.store (sample, std::memory_order_release);
    notifyChanged();
}

void Transport::setLoop (bool on) { loopOn.store (on, std::memory_order_release); notifyChanged(); }

void Transport::setLoopRegion (long long s, long long e)
{
    const long long len = totalLen.load (std::memory_order_acquire);
    s = std::max<long long> (0, std::min (s, len));
    e = std::max<long long> (s + 1, std::min (e, len));
    lpStart.store (s, std::memory_order_release);
    lpEnd.store   (e, std::memory_order_release);
    notifyChanged();
}

void Transport::notifyChanged() const
{
    for (auto& l : listeners) if (l) l();
}

} // namespace stemmerizer::dsp
