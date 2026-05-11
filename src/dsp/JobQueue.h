#pragma once

#include "StemSplitter.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace stemmerizer::dsp
{

/// One unit of work in the batch queue.
struct Job
{
    int                id { 0 };               // unique, monotonic
    std::string        inputPath;              // source file
    std::string        outputDir;              // directory to write stems into
    SplitOptions       options;
    std::vector<bool>  enabledStems;           // length = numStems(options.model)
    std::string        exportFormat = "wav24"; // "wav24" | "wav16" | "wav32f" | "flac" | "mp3"

    // Live state — UI reads these via getJobs().
    enum class State { Queued, Running, Done, Failed, Cancelled };
    State              state    { State::Queued };
    float              progress { 0.f };
    std::string        stage;
    std::string        errorMessage;
    double             elapsedSeconds { 0.0 };
};

/// Owns the worker thread, the splitter, and the queue. The PluginProcessor
/// holds exactly one of these.
class JobQueue
{
public:
    JobQueue();
    ~JobQueue();

    /// Resolve weights & load the model. Called once at startup; cheap to
    /// call again to switch model variants.
    bool ensureModel (const std::string& weightsDir, ModelVariant variant, std::string& errorOut);

    /// Add a job. Returns the assigned ID.
    int  enqueue (Job job);

    /// Cancel a queued or running job by id. No-op if already finished.
    void cancel (int id);

    /// Remove a job from the queue snapshot. Intended for finished rows
    /// (Done / Failed / Cancelled) the user wants to dismiss from the UI.
    /// No-op on Running jobs — call cancel() first and wait for the
    /// Cancelled state. No-op if id isn't in the queue.
    void remove (int id);

    /// Snapshot of the queue for the UI.
    std::vector<Job> snapshot() const;

    /// Set a callback fired on the message thread whenever job state changes.
    /// (We post to the JUCE message manager from inside the worker.)
    /// Thread-safe to call from any thread (typically the editor's ctor /
    /// dtor on the message thread); internally guarded against the worker
    /// invoking the callback at the same time it's being reassigned.
    using ChangeCallback = std::function<void()>;
    void setChangeCallback (ChangeCallback cb);

    /// Set a callback fired when a job finishes successfully, BEFORE the
    /// stems are written to disk. Receives the input path, decoded original
    /// audio (interleaved, original sample rate / channel count), and the
    /// SplitResult. Used by PluginProcessor to populate the in-memory
    /// StemSession so the user can play / mix / drag-export in the plugin.
    using FinishedCallback = std::function<void(const std::string& inputPath,
                                                std::vector<float>&& originalInterleaved,
                                                int sampleRate,
                                                int numChannels,
                                                long long numFrames,
                                                const SplitResult&)>;
    void setFinishedCallback (FinishedCallback cb);

private:
    void workerLoop();
    void notifyChange();

    mutable std::mutex      mutex;
    std::condition_variable cv;
    std::deque<Job>         queue;
    std::atomic<bool>       quit { false };
    // shutdown is distinct from quit: quit asks the worker to leave the
    // outer wait when idle, shutdown forces an in-flight inference to
    // bail out via the per-job cancel watcher. Both go up in ~JobQueue.
    std::atomic<bool>       shutdown { false };
    std::atomic<int>        nextId { 1 };
    std::atomic<int>        cancelId { 0 };
    std::thread             worker;

    StemSplitter            splitter;

    // Callbacks are read on the worker thread and written on the message
    // thread. std::function is not internally synchronized, so concurrent
    // assignment+invoke is UB. Guard with their own mutex (separate from
    // the queue mutex to avoid priority inversion).
    mutable std::mutex      callbackMutex;
    ChangeCallback          onChange;
    FinishedCallback        onFinished;
};

} // namespace stemmerizer::dsp
