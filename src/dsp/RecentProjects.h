#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <mutex>
#include <vector>

namespace stemmerizer::dsp
{

/// One entry in the "recent splits" list — just enough to redrag the
/// resulting stems back into a DAW without re-running the model. We do NOT
/// re-load the source audio or rebuild the StemSession from these.
struct RecentEntry
{
    juce::String      id;             // "timestampMs-counter"
    juce::String      inputPath;
    juce::String      inputBasename;  // for display ("song" out of "song.mp3")
    juce::String      outputDir;
    juce::StringArray stemFiles;      // full paths to written stems on disk
    juce::StringArray stemNames;      // "drums", "bass", "other", "vocals", ...
    juce::int64       timestamp { 0 };
    juce::String      format;         // "wav24" / "wav16" / "wav32f" / "flac" / "mp3"

    /// True iff every stem file still exists on disk. Used by the UI to
    /// disable drag on a card whose stems were deleted out from under us.
    bool stemsExist() const;
};

/// Persistent, bounded list of the most recent successful splits.
///
/// Storage: %APPDATA%/Stemmerizer/recents.json — a single JSON array,
/// newest-first, capped at kMax entries.
///
/// Thread-safety: the public mutators take an internal mutex. `onChanged`
/// fires on whichever thread called the mutator; callers that need it on
/// the message thread should wrap their callback in
/// `juce::MessageManager::callAsync`.
class RecentProjects
{
public:
    RecentProjects();    // loads from disk synchronously in ctor

    /// Snapshot of current entries, newest first. Copied under the lock.
    std::vector<RecentEntry> entries() const;

    /// Append a new entry; drops the oldest if we'd exceed kMax. Dedupes by
    /// inputPath — if an entry with the same inputPath already exists, it's
    /// removed before the new one is prepended. Persists to disk.
    void add (RecentEntry e);

    /// Remove one entry by id. Persists to disk.
    void remove (const juce::String& id);

    /// Drop every entry. Persists to disk.
    void clear();

    /// Path of the JSON file on disk. Stable across runs.
    static juce::File jsonPath();

    /// Maximum number of entries retained.
    static constexpr int kMax = 5;

    /// Called after any mutation (add/remove/clear). Called on whichever
    /// thread modified the list — the editor wraps the body in
    /// MessageManager::callAsync to bounce to the message thread.
    std::function<void()> onChanged;

private:
    void loadFromDisk();
    void saveToDisk();   // atomic: writes to .tmp then renames over the target

    mutable std::mutex       mutex;
    std::vector<RecentEntry> list;
};

} // namespace stemmerizer::dsp
