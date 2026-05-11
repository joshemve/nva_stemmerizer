#pragma once

#include <atomic>
#include <array>
#include <functional>
#include <vector>

namespace stemmerizer::dsp
{

/// Thread-safe per-stem mix state. The audio thread reads with relaxed
/// atomics; the UI writes from the message thread. We use a small fixed
/// upper bound (8) so this struct is allocation-free.
class StemMixState
{
public:
    static constexpr int kMaxStems = 8;

    /// Per-stem fields kept lock-free for the audio thread.
    struct Slot
    {
        std::atomic<float> gain   { 1.0f };   // linear, 0..2 (UI maps -inf..+6dB)
        std::atomic<float> pan    { 0.0f };   // -1..+1
        std::atomic<bool>  muted  { false };
        std::atomic<bool>  soloed { false };
    };

    StemMixState() = default;

    void setStemCount (int n);
    int  stemCount() const noexcept { return numStems; }

    Slot&       slot (int idx)       { return slots[(size_t) idx]; }
    const Slot& slot (int idx) const { return slots[(size_t) idx]; }

    /// True if any slot is soloed (caller's hot-path uses this to decide
    /// whether to apply solo logic).
    bool anySoloed() const noexcept;

    /// Reset to defaults.
    void resetAll();

    // ---- listeners (UI updates) -------------------------------------
    using Listener = std::function<void()>;
    void addListener    (Listener l) { listeners.push_back (std::move (l)); }
    void notifyChanged() const;

private:
    std::array<Slot, kMaxStems> slots;
    int                          numStems { 0 };
    std::vector<Listener>        listeners;
};

} // namespace stemmerizer::dsp
