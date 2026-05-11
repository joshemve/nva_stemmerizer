#include "DragExporter.h"

#include "AudioFileIO.h"

#include <juce_core/juce_core.h>

#include <chrono>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace stemmerizer::dsp
{

namespace
{
    juce::String safeBasename (const std::string& path)
    {
        const auto stem = fs::path (path).stem().string();
        if (stem.empty()) return "stem";
        // Strip characters that are invalid in Windows file names.
        juce::String s (stem);
        return s.removeCharacters ("<>:\"/\\|?*");
    }

    /// Render the in-memory mix (with mix state applied) into one
    /// interleaved buffer at the snapshot's sample rate.
    std::vector<float> renderMixdown (const StemSession::Snapshot& snap,
                                      const StemMixState&          mix)
    {
        const int  ch    = std::max (1, snap.numChannels);
        const long long F = snap.numFrames;
        std::vector<float> out ((size_t) (F * ch), 0.f);

        if (F <= 0 || snap.stems.empty()) return out;

        const bool anySolo = mix.anySoloed();
        const int  N       = (int) snap.stems.size();

        for (int s = 0; s < N; ++s)
        {
            const auto& slot = mix.slot (s);
            if (slot.muted.load()) continue;
            if (anySolo && ! slot.soloed.load()) continue;

            const float gain = slot.gain.load();
            const float pan  = slot.pan.load();
            const float p    = std::max (-1.f, std::min (1.f, pan));
            const float ang  = (p + 1.f) * 0.25f * 3.14159265358979323846f;
            const float gL   = std::cos (ang);
            const float gR   = std::sin (ang);

            const auto& src = snap.stems[(size_t) s];
            const int   sCh = src.numChannels;
            for (long long i = 0; i < F; ++i)
            {
                const float v0 = src.interleaved[(size_t) (i * sCh)];
                const float v1 = sCh >= 2 ? src.interleaved[(size_t) (i * sCh + 1)] : v0;
                if (ch == 1)
                {
                    out[(size_t) i] += 0.5f * (v0 + v1) * gain;
                }
                else
                {
                    out[(size_t) (i * ch)]     += v0 * gain * gL;
                    out[(size_t) (i * ch + 1)] += v1 * gain * gR;
                }
            }
        }
        return out;
    }
}

juce::File DragExporter::scratchDir()
{
    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("Stemmerizer");
    base.createDirectory();

    // Unique-per-process scratch subdir. JUCE 8 doesn't expose getProcessID
    // cross-platform, so we use the launch-time millis instead — collision
    // probability is effectively zero for our purposes.
    static const juce::String pidStr = juce::String ((juce::int64) juce::Time::currentTimeMillis());
    const auto sub = base.getChildFile (pidStr);
    sub.createDirectory();
    return sub;
}

void DragExporter::purgeOldScratchDirs()
{
    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("Stemmerizer");
    if (! base.isDirectory()) return;

    // Same identifier scheme as scratchDir() — purge anything that's not
    // ours. Since the launch-time millis is captured per-process at
    // first call, scratchDir() must have run at least once before this.
    const auto myDir = scratchDir();
    auto kids = base.findChildFiles (juce::File::findDirectories, false);
    for (const auto& k : kids)
        if (k.getFullPathName() != myDir.getFullPathName()) k.deleteRecursively();
}

bool DragExporter::dragStem (juce::Component*               source,
                             const StemSession::Snapshot&   snap,
                             int                            idx,
                             const std::string&             sourceBasename)
{
    if (idx < 0 || idx >= (int) snap.stems.size()) return false;
    const auto& s = snap.stems[(size_t) idx];

    const auto out = scratchDir().getChildFile (
        safeBasename (sourceBasename) + " - " + juce::String (s.name) + ".wav");

    std::string err;
    if (! AudioFileIO::encode (out.getFullPathName().toStdString(),
                                AudioFileIO::ExportFormat::Wav24,
                                s.interleaved.data(),
                                (long long) s.interleaved.size() / s.numChannels,
                                s.numChannels,
                                snap.sampleRate, err))
    {
        return false;
    }

    juce::StringArray files; files.add (out.getFullPathName());
    if (source)
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, source);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false);
    return true;
}

bool DragExporter::dragAllStems (juce::Component*             source,
                                 const StemSession::Snapshot& snap,
                                 const std::string&           sourceBasename)
{
    juce::StringArray files;
    for (size_t i = 0; i < snap.stems.size(); ++i)
    {
        const auto& s = snap.stems[i];
        const auto out = scratchDir().getChildFile (
            safeBasename (sourceBasename) + " - " + juce::String (s.name) + ".wav");
        std::string err;
        if (! AudioFileIO::encode (out.getFullPathName().toStdString(),
                                    AudioFileIO::ExportFormat::Wav24,
                                    s.interleaved.data(),
                                    (long long) s.interleaved.size() / s.numChannels,
                                    s.numChannels,
                                    snap.sampleRate, err))
            continue;
        files.add (out.getFullPathName());
    }
    if (files.isEmpty()) return false;
    if (source)
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, source);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false);
    return true;
}

bool DragExporter::dragMixdown (juce::Component*             source,
                                const StemSession::Snapshot& snap,
                                const StemMixState&          mix,
                                const std::string&           sourceBasename)
{
    auto buf = renderMixdown (snap, mix);
    if (buf.empty()) return false;

    const auto out = scratchDir().getChildFile (
        safeBasename (sourceBasename) + " - mixdown.wav");

    std::string err;
    if (! AudioFileIO::encode (out.getFullPathName().toStdString(),
                                AudioFileIO::ExportFormat::Wav24,
                                buf.data(),
                                snap.numFrames,
                                snap.numChannels,
                                snap.sampleRate, err))
        return false;

    juce::StringArray files; files.add (out.getFullPathName());
    if (source)
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, source);
    else
        juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false);
    return true;
}

} // namespace stemmerizer::dsp
