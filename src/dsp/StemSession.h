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

    /// Atomic, read-mostly snapshot for the audio thread. Holding a
    /// shared_ptr to one of these freezes the buffers in place until the
    /// audio block finishes.
    struct Snapshot
    {
        int                 sampleRate  { 44100 };
        int                 numChannels { 2 };
        long long           numFrames   { 0 };
        std::vector<float>  original;             // input mix (interleaved)
        std::vector<Stem>   stems;
        bool                playOriginal { false }; // A/B compare: when true,
                                                    // route original through.
    };

    /// Returns the current snapshot. The audio thread should grab this
    /// once per processBlock; lifetime is tied to the returned shared_ptr.
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
    using Listener = std::function<void()>;
    void addListener (Listener l);
    void notifyChanged();

private:
    mutable std::mutex                  mutex;
    std::shared_ptr<const Snapshot>     snap { std::make_shared<Snapshot>() };
    std::string                         sourcePath;
    StemMixState                        mix;
    std::atomic<bool>                   playOrig { false };
    std::vector<Listener>               listeners;
};

} // namespace stemmerizer::dsp
