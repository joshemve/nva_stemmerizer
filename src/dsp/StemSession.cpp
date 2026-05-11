#include "StemSession.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>

namespace stemmerizer::dsp
{

StemSession::StemSession()  = default;
StemSession::~StemSession() = default;

std::shared_ptr<const StemSession::Snapshot> StemSession::currentSnapshot() const noexcept
{
    std::lock_guard<std::mutex> lock (mutex);
    return snap;
}

void StemSession::loadFromResult (const std::string&    inputPath,
                                  std::vector<float>    originalInterleaved,
                                  int                   sampleRate,
                                  int                   numChannels,
                                  long long             numFrames,
                                  const SplitResult&    result)
{
    auto fresh = std::make_shared<Snapshot>();
    fresh->sampleRate   = sampleRate;
    fresh->numChannels  = numChannels;
    fresh->numFrames    = numFrames;
    fresh->original     = std::move (originalInterleaved);
    fresh->playOriginal = playOrig.load();

    fresh->stems.reserve (result.stems.size());
    for (const auto& s : result.stems)
    {
        Stem out;
        out.name        = s.name;
        out.numChannels = s.numChannels;
        out.interleaved = s.interleaved;       // copy — small price for safety
        fresh->stems.push_back (std::move (out));
    }

    {
        std::lock_guard<std::mutex> lock (mutex);
        snap       = fresh;
        sourcePath = inputPath;
    }

    mix.setStemCount ((int) fresh->stems.size());
    notifyChanged();
}

void StemSession::clear()
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        snap       = std::make_shared<Snapshot>();
        sourcePath.clear();
    }
    mix.setStemCount (0);
    notifyChanged();
}

bool StemSession::isLoaded() const noexcept
{
    std::lock_guard<std::mutex> lock (mutex);
    return snap && snap->numFrames > 0 && ! snap->stems.empty();
}

std::string StemSession::sourceFilePath() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return sourcePath;
}

void StemSession::setPlayOriginal (bool b)
{
    playOrig.store (b);
    // Mutate the current snapshot's flag so the audio thread sees it
    // without us needing to allocate a new snapshot just for an A/B toggle.
    // Safe because Snapshot::playOriginal is only consumed by the audio
    // thread and we only flip a bool here (atomic by virtue of being POD
    // single byte aligned + write-then-flag pattern).
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (snap)
        {
            auto fresh = std::make_shared<Snapshot> (*snap);
            fresh->playOriginal = b;
            snap = fresh;
        }
    }
    notifyChanged();
}

bool StemSession::playOriginal() const { return playOrig.load(); }

StemSession::ListenerHandle StemSession::addListener (Listener l)
{
    std::lock_guard<std::mutex> lock (mutex);
    const auto id = nextListenerId.fetch_add (1);
    listeners.push_back ({ id, std::move (l) });
    return id;
}

void StemSession::removeListener (ListenerHandle h)
{
    std::lock_guard<std::mutex> lock (mutex);
    listeners.erase (std::remove_if (listeners.begin(), listeners.end(),
                                     [h] (const ListenerSlot& s) { return s.id == h; }),
                     listeners.end());
}

void StemSession::notifyChanged()
{
    // Snapshot under the lock so the async dispatch can't race with
    // add/remove. The captured copy holds its own owning shared_ptr-free
    // function objects (no `this`-of-listener-vector reference).
    std::vector<Listener> cbs;
    {
        std::lock_guard<std::mutex> lock (mutex);
        cbs.reserve (listeners.size());
        for (auto& s : listeners) cbs.push_back (s.fn);
    }
    juce::MessageManager::callAsync ([cbs = std::move (cbs)]
    {
        for (auto& l : cbs) if (l) l();
    });
}

} // namespace stemmerizer::dsp
