#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace stemmerizer::dsp
{

/// Lock-free transport for the in-plugin player. The audio thread reads
/// position/loop/playing state with atomics; the UI writes via the public
/// methods. Position is kept in samples at the session's sample rate plus
/// a [0, 1) sub-sample fractional accumulator — see commitBlock() for why.
class Transport
{
public:
    Transport() = default;

    // ---- audio-thread API (read only) -------------------------------
    bool      isPlaying() const noexcept    { return playing.load (std::memory_order_acquire); }
    long long position()  const noexcept    { return pos.load (std::memory_order_acquire); }
    bool      isLoopOn()  const noexcept    { return loopOn.load (std::memory_order_acquire); }
    long long loopStart() const noexcept    { return lpStart.load (std::memory_order_acquire); }
    long long loopEnd()   const noexcept    { return lpEnd.load (std::memory_order_acquire); }
    long long length()    const noexcept    { return totalLen.load (std::memory_order_acquire); }
    int       sampleRate() const noexcept   { return sr.load (std::memory_order_acquire); }

    /// Sub-sample fractional position, [0, 1). Only meaningful when host
    /// SR differs from session SR — MixRenderer accumulates the fraction
    /// across blocks here so playback doesn't drift sample-by-sample
    /// when ratio is non-integer (e.g. 44.1k session in a 48k host).
    /// Reset to 0 on any seek / stop / prepare since those land on
    /// integer source samples.
    double    phaseFrac() const noexcept    { return phase.load (std::memory_order_acquire); }

    /// MixRenderer calls this once at the end of every block instead of
    /// the old advance(delta). Atomic-CAS so a concurrent seek wins:
    ///   * `expectedFrom` is the integer pos the renderer captured at
    ///     block start. If pos has changed since then (the user seeked
    ///     mid-block), commitBlock leaves pos alone and resets the
    ///     fractional accumulator so the next block starts clean.
    ///   * On success, writes the new integer pos AND the new sub-sample
    ///     fraction; triggers auto-stop at end-of-stream if loop is off.
    /// Returns true if the commit landed (no concurrent seek), false if
    /// it was rejected. The audio thread can ignore the return.
    bool commitBlock (long long expectedFrom,
                      long long newPosInt,
                      double    newPhaseFrac) noexcept;

    // ---- message-thread API (write) ---------------------------------
    void prepare (long long lengthInSamples, int sampleRateHz);
    void play();
    void pause();
    void stop();           // pause + seek to start (or loop start, if loop on)
    void seek (long long sample);
    void setLoop (bool on);
    void setLoopRegion (long long startSample, long long endSample);

    // ---- listeners --------------------------------------------------
    // ID-based registration so transient owners can deregister cleanly
    // (matches StemSession::addListener — see that header for rationale).
    using Listener       = std::function<void()>;
    using ListenerHandle = std::uint64_t;
    ListenerHandle addListener    (Listener l);
    void           removeListener (ListenerHandle h);
    void           notifyChanged() const;

private:
    struct ListenerSlot { ListenerHandle id; Listener fn; };

    std::atomic<bool>      playing  { false };
    std::atomic<bool>      loopOn   { false };
    std::atomic<long long> pos      { 0 };
    // phase is the audio-thread's running [0, 1) sub-sample remainder.
    // Audio thread is its sole writer (via commitBlock); UI never touches
    // it directly. Stored as atomic<double> so debug/UI reads are safe.
    std::atomic<double>    phase    { 0.0 };
    std::atomic<long long> lpStart  { 0 };
    std::atomic<long long> lpEnd    { 0 };
    std::atomic<long long> totalLen { 0 };
    std::atomic<int>       sr       { 44100 };

    mutable std::mutex            listenerMutex;
    std::vector<ListenerSlot>     listeners;
    std::atomic<ListenerHandle>   nextListenerId { 1 };
};

} // namespace stemmerizer::dsp
