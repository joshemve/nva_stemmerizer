#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace stemmerizer::dsp
{

/// Lock-free transport for the in-plugin player. The audio thread reads
/// position/loop/playing state with atomics; the UI writes via the public
/// methods. Position is kept in samples at the session's sample rate.
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

    /// The audio thread calls this once it has rendered `n` samples to
    /// advance the position (and wrap into the loop region if needed).
    /// Returns the new position.
    long long advance (long long n) noexcept;

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
    std::atomic<long long> lpStart  { 0 };
    std::atomic<long long> lpEnd    { 0 };
    std::atomic<long long> totalLen { 0 };
    std::atomic<int>       sr       { 44100 };

    mutable std::mutex            listenerMutex;
    std::vector<ListenerSlot>     listeners;
    std::atomic<ListenerHandle>   nextListenerId { 1 };
};

} // namespace stemmerizer::dsp
