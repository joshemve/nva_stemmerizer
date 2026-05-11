#include "RecentProjects.h"

#include <algorithm>
#include <memory>

namespace stemmerizer::dsp
{

// ---------------------------------------------------------------------------
// RecentEntry
// ---------------------------------------------------------------------------
bool RecentEntry::stemsExist() const
{
    if (stemFiles.isEmpty()) return false;
    for (const auto& p : stemFiles)
    {
        if (p.isEmpty()) return false;
        if (! juce::File (p).existsAsFile()) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// (de)serialisation
// ---------------------------------------------------------------------------
namespace
{
    juce::var entryToVar (const RecentEntry& e)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("id",            e.id);
        obj->setProperty ("inputPath",     e.inputPath);
        obj->setProperty ("inputBasename", e.inputBasename);
        obj->setProperty ("outputDir",     e.outputDir);

        juce::Array<juce::var> files;
        for (const auto& f : e.stemFiles) files.add (f);
        obj->setProperty ("stemFiles", files);

        juce::Array<juce::var> names;
        for (const auto& n : e.stemNames) names.add (n);
        obj->setProperty ("stemNames", names);

        obj->setProperty ("timestamp", (juce::int64) e.timestamp);
        obj->setProperty ("format",    e.format);
        return juce::var (obj);
    }

    bool varToEntry (const juce::var& v, RecentEntry& out)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) return false;

        out.id            = obj->getProperty ("id").toString();
        out.inputPath     = obj->getProperty ("inputPath").toString();
        out.inputBasename = obj->getProperty ("inputBasename").toString();
        out.outputDir     = obj->getProperty ("outputDir").toString();
        out.timestamp     = (juce::int64) obj->getProperty ("timestamp");
        out.format        = obj->getProperty ("format").toString();

        out.stemFiles.clear();
        if (auto* arr = obj->getProperty ("stemFiles").getArray())
            for (const auto& s : *arr) out.stemFiles.add (s.toString());

        out.stemNames.clear();
        if (auto* arr = obj->getProperty ("stemNames").getArray())
            for (const auto& s : *arr) out.stemNames.add (s.toString());

        // id is the only field we genuinely require to be non-empty — it
        // identifies the row for remove(). Missing-everything-else is OK
        // (we render gracefully).
        return out.id.isNotEmpty();
    }
}

// ---------------------------------------------------------------------------
// RecentProjects
// ---------------------------------------------------------------------------
RecentProjects::RecentProjects()
{
    loadFromDisk();
}

std::vector<RecentEntry> RecentProjects::entries() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return list;
}

void RecentProjects::add (RecentEntry e)
{
    {
        std::lock_guard<std::mutex> lock (mutex);

        // Dedupe by inputPath — the most common cause of a duplicate is the
        // user re-splitting the same file. Keep the newest.
        if (e.inputPath.isNotEmpty())
        {
            list.erase (std::remove_if (list.begin(), list.end(),
                            [&] (const RecentEntry& x)
                            { return x.inputPath == e.inputPath; }),
                        list.end());
        }

        list.insert (list.begin(), std::move (e));

        if ((int) list.size() > kMax)
            list.resize ((size_t) kMax);
    }
    saveToDisk();
    if (onChanged) onChanged();
}

void RecentProjects::remove (const juce::String& id)
{
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock (mutex);
        const auto before = list.size();
        list.erase (std::remove_if (list.begin(), list.end(),
                        [&] (const RecentEntry& x) { return x.id == id; }),
                    list.end());
        changed = (list.size() != before);
    }
    if (changed)
    {
        saveToDisk();
        if (onChanged) onChanged();
    }
}

void RecentProjects::clear()
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (list.empty()) return;
        list.clear();
    }
    saveToDisk();
    if (onChanged) onChanged();
}

juce::File RecentProjects::jsonPath()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Stemmerizer")
               .getChildFile ("recents.json");
}

void RecentProjects::loadFromDisk()
{
    const auto f = jsonPath();
    if (! f.existsAsFile()) return;

    juce::var parsed;
    {
        // Read and parse defensively — a corrupt file should just yield an
        // empty list, not crash plugin construction.
        const auto txt = f.loadFileAsString();
        if (txt.isEmpty()) return;
        try { parsed = juce::JSON::parse (txt); }
        catch (...) { return; }
    }

    auto* arr = parsed.getArray();
    if (arr == nullptr) return;

    std::vector<RecentEntry> loaded;
    loaded.reserve ((size_t) arr->size());
    for (const auto& v : *arr)
    {
        RecentEntry e;
        if (varToEntry (v, e))
            loaded.push_back (std::move (e));
        if ((int) loaded.size() >= kMax) break;
    }

    std::lock_guard<std::mutex> lock (mutex);
    list = std::move (loaded);
}

void RecentProjects::saveToDisk()
{
    // Build the JSON outside the lock window — keep the mutex held only for
    // the snapshot copy.
    std::vector<RecentEntry> snapshot;
    {
        std::lock_guard<std::mutex> lock (mutex);
        snapshot = list;
    }

    juce::Array<juce::var> arr;
    for (const auto& e : snapshot) arr.add (entryToVar (e));

    const auto json = juce::JSON::toString (juce::var (arr), false /* pretty */);

    const auto target = jsonPath();
    target.getParentDirectory().createDirectory();

    // Atomic-ish write: write to a sibling .tmp file then move-replace.
    // If two plugin instances race here, last writer wins — acceptable for
    // a low-frequency, recoverable list.
    const auto tmp = target.getSiblingFile (target.getFileName() + ".tmp");
    tmp.deleteFile();
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (tmp.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        stream->writeText (json, false, false, "\n");
        stream->flush();
        stream.reset();   // close the handle before we try to move it

        if (! tmp.moveFileTo (target))
        {
            // Move failed (target locked, etc.) — fall back to direct
            // overwrite so we at least get a recoverable file on disk.
            target.replaceWithText (json);
            tmp.deleteFile();
        }
    }
    else
    {
        // Couldn't even open the tmp file (read-only profile? quota?). Try
        // the direct replace as a best-effort fallback.
        target.replaceWithText (json);
    }
}

} // namespace stemmerizer::dsp
