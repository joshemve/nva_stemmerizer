#include "StemMixState.h"

namespace stemmerizer::dsp
{

void StemMixState::setStemCount (int n)
{
    numStems = n < 0 ? 0 : (n > kMaxStems ? kMaxStems : n);
    resetAll();
}

bool StemMixState::anySoloed() const noexcept
{
    for (int i = 0; i < numStems; ++i)
        if (slots[(size_t) i].soloed.load (std::memory_order_relaxed))
            return true;
    return false;
}

void StemMixState::resetAll()
{
    for (auto& s : slots)
    {
        s.gain.store   (1.0f, std::memory_order_relaxed);
        s.pan.store    (0.0f, std::memory_order_relaxed);
        s.muted.store  (false, std::memory_order_relaxed);
        s.soloed.store (false, std::memory_order_relaxed);
    }
}

void StemMixState::notifyChanged() const
{
    for (auto& l : listeners) if (l) l();
}

} // namespace stemmerizer::dsp
