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
    quit = true;
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
    {
        std::lock_guard<std::mutex> lock (mutex);
        job.id    = nextId.fetch_add (1);
        job.state = Job::State::Queued;
        queue.push_back (std::move (job));
    }
    cv.notify_one();
    notifyChange();
    return nextId.load() - 1;
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

std::vector<Job> JobQueue::snapshot() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return { queue.begin(), queue.end() };
}

void JobQueue::notifyChange()
{
    if (! onChange) return;
    juce::MessageManager::callAsync ([cb = onChange] { if (cb) cb(); });
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
        notifyChange();

        std::atomic<bool> cancelFlag { false };
        const int runningId = current.id;

        // Watcher: flips cancelFlag if the user requests cancel.
        std::thread cancelWatcher ([this, &cancelFlag, runningId]
        {
            while (! cancelFlag.load() && ! quit.load())
            {
                if (cancelId.load() == runningId) { cancelFlag = true; break; }
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

        // Hand the finished split off to the in-plugin session BEFORE we
        // write to disk — that way the user can play stems even while the
        // disk write is still in flight.
        if (result.success && onFinished)
        {
            std::vector<float> originalCopy = decoded.interleaved;
            onFinished (current.inputPath,
                        std::move (originalCopy),
                        decoded.sampleRate,
                        decoded.numChannels,
                        decoded.numFrames,
                        result);
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
    }
}

} // namespace stemmerizer::dsp
