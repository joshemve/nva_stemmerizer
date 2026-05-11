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
    // Lock-free atomic load — safe to call from the audio thread.
    return snap.load();
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

    fresh->stems.reserve (result.stems.size());
    for (const auto& s : result.stems)
    {
        Stem out;
        out.name        = s.name;
        out.numChannels = s.numChannels;
        out.interleaved = s.interleaved;       // copy — small price for safety
        fresh->stems.push_back (std::move (out));
    }

    // sourcePath is small and the mutex is fine. The snapshot pointer
    // itself swap is lock-free.
    {
        std::lock_guard<std::mutex> lock (mutex);
        sourcePath = inputPath;
    }
    snap.store (std::move (fresh));

    mix.setStemCount ((int) result.stems.size());
    notifyChanged();
}

void StemSession::clear()
{
    snap.store (std::make_shared<Snapshot>());
    {
        std::lock_guard<std::mutex> lock (mutex);
        sourcePath.clear();
    }
    mix.setStemCount (0);
    notifyChanged();
}

bool StemSession::isLoaded() const noexcept
{
    auto s = snap.load();
    return s && s->numFrames > 0 && ! s->stems.empty();
}

std::string StemSession::sourceFilePath() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return sourcePath;
}

void StemSession::setPlayOriginal (bool b)
{
    // Just flip the atomic — no Snapshot copy. The audio thread + UI both
    // read this via playOriginal() at the start of each block / paint.
    playOrig.store (b);
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
    std::vector<Listener> cbs;
    {
        std::lock_guard<std::mutex> lock (mutex);
        cbs.reserve (listeners.size());
        for (auto& s : listeners) cbs.push_back (s.fn);
    }
    juce::MessageManager::callAsync ([cbs = std::move (cbs)]
    {
        // callAsync runs on the message thread inside a noexcept
        // trampoline. If any listener throws (mixer.rebuild bad_alloc,
        // a stale weak ref, anything), the exception escapes the
        // trampoline and the host crashes with FAST_FAIL. Guard each
        // call independently so one bad listener doesn't suppress the
        // rest.
        for (auto& l : cbs)
        {
            if (! l) continue;
            try { l(); }
            catch (...) { /* listener self-reported a bug; carry on */ }
        }
    });
}

} // namespace stemmerizer::dsp
