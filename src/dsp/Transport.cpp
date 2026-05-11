#include "Transport.h"

#include <algorithm>

namespace stemmerizer::dsp
{

bool Transport::commitBlock (long long expectedFrom,
                             long long newPosInt,
                             double    newPhaseFrac) noexcept
{
    // CAS pos from `expectedFrom` to `newPosInt`. If the user seeked
    // mid-block, pos has already moved, and we MUST NOT overwrite that
    // seek with our renderer-side advance (audit N5). On rejection we
    // also wipe the fractional accumulator — the seek landed on an
    // integer source sample, so any leftover sub-sample remainder from
    // the prior block is stale.
    long long expected = expectedFrom;
    if (! pos.compare_exchange_strong (expected, newPosInt,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
    {
        phase.store (0.0, std::memory_order_release);
        return false;
    }

    // Pos updated. Carry the new sub-sample fraction forward so the next
    // block doesn't truncate it to zero (audit N2). The two stores are
    // intentionally unordered relative to each other — only the audio
    // thread writes either, and any UI reader of `phase` is purely
    // cosmetic (no consumer today).
    phase.store (newPhaseFrac, std::memory_order_release);

    // Auto-stop at end-of-stream (no loop). Mirrors the old advance()
    // behaviour; the renderer's per-sample silence-past-end path handles
    // the audible part, this is just the playing/pos cleanup.
    const long long len = totalLen.load (std::memory_order_acquire);
    if (len > 0 && newPosInt >= len && ! loopOn.load (std::memory_order_acquire))
    {
        pos.store     (len,  std::memory_order_release);
        phase.store   (0.0,  std::memory_order_release);
        playing.store (false, std::memory_order_release);
    }

    return true;
}

void Transport::prepare (long long lengthInSamples, int sampleRateHz)
{
    totalLen.store (lengthInSamples, std::memory_order_release);
    sr.store       (sampleRateHz,    std::memory_order_release);

    pos.store     (0, std::memory_order_release);
    phase.store   (0.0, std::memory_order_release);
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
    pos.store   (s,   std::memory_order_release);
    phase.store (0.0, std::memory_order_release);
    notifyChanged();
}

void Transport::seek (long long sample)
{
    const long long len = totalLen.load (std::memory_order_acquire);
    sample = std::max<long long> (0, std::min (sample, len));
    pos.store   (sample, std::memory_order_release);
    // Seek targets an integer source sample, so any carried-over
    // sub-sample fraction from the prior playback position is no longer
    // meaningful.
    phase.store (0.0, std::memory_order_release);
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

Transport::ListenerHandle Transport::addListener (Listener l)
{
    std::lock_guard<std::mutex> lock (listenerMutex);
    const auto id = nextListenerId.fetch_add (1);
    listeners.push_back ({ id, std::move (l) });
    return id;
}

void Transport::removeListener (ListenerHandle h)
{
    std::lock_guard<std::mutex> lock (listenerMutex);
    listeners.erase (std::remove_if (listeners.begin(), listeners.end(),
                                     [h] (const ListenerSlot& s) { return s.id == h; }),
                     listeners.end());
}

void Transport::notifyChanged() const
{
    std::vector<Listener> cbs;
    {
        std::lock_guard<std::mutex> lock (listenerMutex);
        cbs.reserve (listeners.size());
        for (auto& s : listeners) cbs.push_back (s.fn);
    }
    for (auto& l : cbs) if (l) l();
}

} // namespace stemmerizer::dsp
