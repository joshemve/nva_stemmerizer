#include "JobQueue.h"

#include "AudioFileIO.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace stemmerizer::dsp
{

JobQueue::JobQueue()
{
    worker = std::thread ([this] { workerLoop(); });
}

JobQueue::~JobQueue()
{
    // Coordinated shutdown (audit N8). The worker can be deep inside
    // splitter.split() at this point — that call only checks the cancel
    // flag between segments via the bridge callback in OnnxBackend, so
    // setting `quit` alone would block the destructor until the current
    // segment finishes (multi-second on CPU). We:
    //   1. Raise `quit` so the worker exits its outer wait when idle.
    //   2. Raise `shutdown` so the per-job cancel watcher (which polls
    //      every 60 ms) trips the local cancelFlag — the inference
    //      bridge sees it on its next callback and throws out.
    //   3. Notify the cv so an idle worker wakes up immediately.
    // Net latency to close the DAW with a long split in flight: up to
    // ~60 ms (watcher poll) + one segment of inference time, instead
    // of waiting for the entire file to finish.
    quit.store     (true);
    shutdown.store (true);
    cv.notify_all();
    if (worker.joinable()) worker.join();
}

bool JobQueue::ensureModel (const std::string& weightsDir, ModelVariant variant, std::string& errorOut)
{
    if (splitter.isModelLoaded() && splitter.currentModel() == variant)
        return true;

    if (! splitter.loadModel (weightsDir, variant))
    {
        errorOut = splitter.errorMessage();
        return false;
    }
    return true;
}

int JobQueue::enqueue (Job job)
{
    // Audit N11: capture the assigned id BEFORE moving `job` into the
    // queue. Previously we returned `nextId.load() - 1` which only
    // happens to be correct under the single-writer guarantee — fragile
    // and confusing. The reservation also stays inside the lock so the
    // returned id is unambiguously tied to the just-enqueued entry.
    int assignedId;
    {
        std::lock_guard<std::mutex> lock (mutex);
        assignedId = nextId.fetch_add (1);
        job.id     = assignedId;
        job.state  = Job::State::Queued;
        queue.push_back (std::move (job));
    }
    cv.notify_one();
    notifyChange();
    return assignedId;
}

void JobQueue::cancel (int id)
{
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock (mutex);
        for (auto it = queue.begin(); it != queue.end(); ++it)
        {
            if (it->id == id)
            {
                if (it->state == Job::State::Queued)
                {
                    it->state = Job::State::Cancelled;
                    removed = true;
                }
                else if (it->state == Job::State::Running)
                {
                    cancelId.store (id);
                }
                break;
            }
        }
    }
    if (removed) notifyChange();
}

void JobQueue::remove (int id)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock (mutex);
        for (auto it = queue.begin(); it != queue.end(); ++it)
        {
            if (it->id == id)
            {
                // Don't erase a job the worker is actively processing —
                // it would lose visibility of the cancel pathway. Caller
                // should cancel() first and wait for Cancelled state.
                if (it->state == Job::State::Running) return;
                queue.erase (it);
                changed = true;
                break;
            }
        }
    }
    if (changed) notifyChange();
}

std::vector<Job> JobQueue::snapshot() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return { queue.begin(), queue.end() };
}

void JobQueue::setChangeCallback (ChangeCallback cb)
{
    std::lock_guard<std::mutex> lock (callbackMutex);
    onChange = std::move (cb);
}

void JobQueue::setFinishedCallback (FinishedCallback cb)
{
    std::lock_guard<std::mutex> lock (callbackMutex);
    onFinished = std::move (cb);
}

void JobQueue::notifyChange()
{
    // Copy the callback under the lock — std::function copy is not
    // synchronized internally and we'd race a concurrent set*Callback.
    ChangeCallback cbCopy;
    {
        std::lock_guard<std::mutex> lock (callbackMutex);
        cbCopy = onChange;
    }
    if (! cbCopy) return;
    juce::MessageManager::callAsync ([cb = std::move (cbCopy)]
    {
        // Same noexcept-trampoline guard as StemSession::notifyChanged.
        if (! cb) return;
        try { cb(); } catch (...) { /* swallow — log only */ }
    });
}

namespace
{
    AudioFileIO::ExportFormat parseFormat (const std::string& s)
    {
        if (s == "wav16")  return AudioFileIO::ExportFormat::Wav16;
        if (s == "wav32f") return AudioFileIO::ExportFormat::Wav32f;
        if (s == "flac")   return AudioFileIO::ExportFormat::Flac;
        if (s == "mp3")    return AudioFileIO::ExportFormat::Mp3;
        return AudioFileIO::ExportFormat::Wav24;
    }

    std::string formatExtension (const std::string& s)
    {
        if (s == "flac") return ".flac";
        if (s == "mp3")  return ".mp3";
        return ".wav";
    }
}

void JobQueue::workerLoop()
{
    while (! quit.load())
    {
        // Outer try/catch covers ONE iteration of the loop. Any uncaught
        // C++ exception escaping a std::thread function calls std::terminate
        // and kills the whole host. Mark the running job as Failed with
        // the exception text instead, so the UI can report it cleanly.
        // We pull `current.id` out so the catch block can update the
        // matching queue entry by id.
        int currentIdForCatch = 0;
        try
        {
        Job current;
        bool haveJob = false;

        {
            std::unique_lock<std::mutex> lock (mutex);
            cv.wait (lock, [this] { return quit.load() || ! queue.empty(); });
            if (quit.load()) return;

            for (auto& j : queue)
            {
                if (j.state == Job::State::Queued)
                {
                    j.state    = Job::State::Running;
                    j.progress = 0.f;
                    j.stage    = "starting";
                    current    = j;
                    haveJob    = true;
                    break;
                }
            }
        }

        if (! haveJob) continue;
        currentIdForCatch = current.id;
        notifyChange();

        std::atomic<bool> cancelFlag { false };
        const int runningId = current.id;

        // Watcher: flips cancelFlag if the user requests cancel — OR if
        // we're being torn down (audit N8). The shutdown branch lets
        // ~JobQueue() unblock a long in-flight inference within the next
        // poll interval instead of waiting for the file to finish.
        std::thread cancelWatcher ([this, &cancelFlag, runningId]
        {
            while (! cancelFlag.load() && ! quit.load())
            {
                if (cancelId.load() == runningId || shutdown.load())
                {
                    cancelFlag = true;
                    break;
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (60));
            }
        });

        auto onProgress = [this, runningId](const ProgressReport& r)
        {
            std::lock_guard<std::mutex> lock (mutex);
            for (auto& j : queue)
            {
                if (j.id == runningId)
                {
                    j.progress = r.fraction;
                    j.stage    = r.stage;
                    break;
                }
            }
            notifyChange();
        };

        // Decode the file once here (instead of letting splitFile do it
        // internally) so we can hand the decoded "original" audio off to
        // the in-memory StemSession after the split finishes.
        AudioFileIO::DecodedAudio decoded;
        std::string decodeErr;
        SplitResult result;

        if (! AudioFileIO::decode (current.inputPath, decoded, decodeErr))
        {
            result.errorMessage = "Failed to decode input: " + decodeErr;
        }
        else
        {
            result = splitter.split (decoded.interleaved.data(),
                                     decoded.numFrames,
                                     decoded.numChannels,
                                     decoded.sampleRate,
                                     current.options,
                                     onProgress,
                                     cancelFlag);
        }

        cancelFlag = true;
        if (cancelWatcher.joinable()) cancelWatcher.join();
        cancelId.store (0);

        // Audit N8: if shutdown was requested while inference ran, skip
        // the publish + disk-write phases entirely. The session and
        // processor are still alive at this point (queue is destroyed
        // before sess/tport per declaration order), but spending another
        // 100+ ms writing WAVs during DAW teardown is pure latency for
        // no observable effect — the next worker iteration will exit
        // anyway when it rechecks `quit`.
        if (shutdown.load()) break;

        // Hand the finished split off to the in-plugin session BEFORE we
        // write to disk — that way the user can play stems even while the
        // disk write is still in flight. Copy the callback under the lock
        // (H1: std::function copy/assign is not internally synchronized).
        // Also note: we MOVE decoded.interleaved here (no defensive copy);
        // decoded is local to this iteration and not used afterwards.
        if (result.success)
        {
            FinishedCallback finishedCopy;
            {
                std::lock_guard<std::mutex> lock (callbackMutex);
                finishedCopy = onFinished;
            }
            if (finishedCopy)
            {
                finishedCopy (current.inputPath,
                              std::move (decoded.interleaved),
                              decoded.sampleRate,
                              decoded.numChannels,
                              decoded.numFrames,
                              result);
            }
        }

        // Write enabled stems out, if successful.
        if (result.success)
        {
            try { fs::create_directories (current.outputDir); } catch (...) {}

            const fs::path basename = fs::path (current.inputPath).stem();
            const auto fmt = parseFormat (current.exportFormat);
            const auto ext = formatExtension (current.exportFormat);

            for (size_t s = 0; s < result.stems.size(); ++s)
            {
                const bool enabled = s < current.enabledStems.size() ? current.enabledStems[s] : true;
                if (! enabled) continue;

                const auto& stem = result.stems[s];
                const auto outPath = (fs::path (current.outputDir)
                                      / (basename.string() + " - " + stem.name + ext)).string();

                std::string err;
                if (! AudioFileIO::encode (outPath, fmt, stem.interleaved.data(),
                                           stem.numFrames(), stem.numChannels,
                                           stem.sampleRate, err))
                {
                    result.success      = false;
                    result.errorMessage = "Failed writing " + stem.name + ": " + err;
                    break;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock (mutex);
            for (auto& j : queue)
            {
                if (j.id == runningId)
                {
                    if (cancelFlag.load() && ! result.success && result.errorMessage == "Cancelled.")
                        j.state = Job::State::Cancelled;
                    else if (result.success)
                        j.state = Job::State::Done;
                    else
                        j.state = Job::State::Failed;

                    j.errorMessage   = result.errorMessage;
                    j.elapsedSeconds = result.processingSeconds;
                    j.progress       = result.success ? 1.f : j.progress;
                    j.stage          = result.success ? "done" : j.stage;
                    break;
                }
            }
        }
        notifyChange();
        }  // end outer try (per-iteration)
        catch (const std::exception& e)
        {
            // Mark the running job as Failed and continue. NEVER allow an
            // exception to escape the worker thread function — that would
            // call std::terminate on the entire host process.
            std::lock_guard<std::mutex> lock (mutex);
            for (auto& j : queue)
            {
                if (j.id == currentIdForCatch)
                {
                    j.state        = Job::State::Failed;
                    j.errorMessage = std::string ("Worker exception: ") + e.what();
                    break;
                }
            }
            notifyChange();
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock (mutex);
            for (auto& j : queue)
            {
                if (j.id == currentIdForCatch)
                {
                    j.state        = Job::State::Failed;
                    j.errorMessage = "Worker hit an unknown exception.";
                    break;
                }
            }
            notifyChange();
        }
    }
}

} // namespace stemmerizer::dsp
