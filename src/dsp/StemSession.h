#pragma once

#include "StemMixState.h"
#include "StemSplitter.h"     // for SplitResult

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace stemmerizer::dsp
{

/// Owns the audio data for the currently loaded split: the original mix
/// (for A/B compare and waveform display) plus one buffer per stem. Also
/// exposes per-stem mix state to the audio thread and notifies the UI on
/// load.
///
/// Threading model:
///   - `loadFromResult()` is called from the worker thread when a split
///     finishes. It atomically swaps in the new buffers under a mutex.
///   - The audio thread reads `currentSnapshot()` once per processBlock
///     and never touches the mutex during steady-state playback.
///   - The UI observes via Listener callbacks dispatched from the
///     message thread.
class StemSession
{
public:
    StemSession();
    ~StemSession();

    /// Stems are stored interleaved (numChannels x numFrames).
    struct Stem
    {
        std::string         name;
        int                 numChannels { 2 };
        std::vector<float>  interleaved;
    };

    /// Immutable, read-mostly snapshot of one loaded split. Stored on the
    /// session as an atomic<shared_ptr>; the audio thread + UI both grab
    /// their own owning copy and the buffers stay alive as long as anyone
    /// holds a reference. NEVER mutate a published Snapshot — always
    /// allocate a fresh one and store-replace.
    struct Snapshot
    {
        int                 sampleRate  { 44100 };
        int                 numChannels { 2 };
        long long           numFrames   { 0 };
        std::vector<float>  original;             // input mix (interleaved)
        std::vector<Stem>   stems;
        // NOTE: playOriginal lives on StemSession (std::atomic<bool>), NOT
        // in here. Was here originally; a previous setPlayOriginal()
        // implementation deep-copied the whole Snapshot (incl. hundreds of
        // MB of audio) just to flip a bool, freeing the prior buffers and
        // turning every WaveformStrip raw pointer into a dangling read.
    };

    /// Returns the current snapshot. Lock-free atomic load — callers
    /// hold a strong shared_ptr so the underlying buffers stay alive
    /// until they drop it, even if a new session is loaded in parallel.
    std::shared_ptr<const Snapshot> currentSnapshot() const noexcept;

    /// Replace contents from a finished split. Called on the worker thread.
    void loadFromResult (const std::string&    inputPath,
                         std::vector<float>    originalInterleaved,
                         int                   sampleRate,
                         int                   numChannels,
                         long long             numFrames,
                         const SplitResult&    result);

    /// Drop everything (e.g. user closed the file).
    void clear();

    /// True if a session is currently loaded.
    bool isLoaded() const noexcept;

    /// Currently loaded source file path (for window title / drag-export
    /// naming). Empty if !isLoaded().
    std::string sourceFilePath() const;

    /// Per-stem mix state (UI bindings target this).
    StemMixState&       mixState()       { return mix; }
    const StemMixState& mixState() const { return mix; }

    /// A/B-compare toggle (read by the audio thread).
    void setPlayOriginal (bool b);
    bool playOriginal() const;

    // ---- listeners --------------------------------------------------
    // ID-based registration so transient owners (editors, panels) can
    // deregister in their destructors and avoid the stale-`this` deref
    // that crashed FL on plugin re-open. addListener() returns a handle;
    // removeListener(handle) is a no-op if already removed.
    using Listener        = std::function<void()>;
    using ListenerHandle  = std::uint64_t;
    ListenerHandle addListener    (Listener l);
    void           removeListener (ListenerHandle h);
    void           notifyChanged();

private:
    struct ListenerSlot { ListenerHandle id; Listener fn; };

    // `snap` is read on the audio thread every block and written from the
    // worker thread when a split finishes. std::atomic<shared_ptr<T>> gives
    // us a lock-free, allocation-free swap. C++20 specifies it as part of
    // the standard.
    std::atomic<std::shared_ptr<const Snapshot>> snap { std::make_shared<Snapshot>() };

    // Everything below is touched only from the message/worker threads,
    // never from the audio thread, so the regular mutex is fine.
    mutable std::mutex                  mutex;
    std::string                         sourcePath;
    StemMixState                        mix;
    std::atomic<bool>                   playOrig { false };
    std::vector<ListenerSlot>           listeners;
    std::atomic<ListenerHandle>         nextListenerId { 1 };
};

} // namespace stemmerizer::dsp
