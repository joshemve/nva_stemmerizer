#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "dsp/MixRenderer.h"
#include "dsp/DragExporter.h"

#include <filesystem>

namespace stemmerizer
{

namespace
{
    /// File extension corresponding to one of our exportFormat tokens, kept
    /// in sync with the table in JobQueue.cpp (parseFormat / formatExtension).
    /// Centralised here so RecentProjects can reconstruct the on-disk stem
    /// paths without having to peek into the worker.
    juce::String extensionForFormat (const juce::String& fmt)
    {
        if (fmt == "flac") return ".flac";
        if (fmt == "mp3")  return ".mp3";
        return ".wav";  // wav16 / wav24 / wav32f all share .wav
    }
}

StemmerizerProcessor::StemmerizerProcessor()
    : juce::AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Default UI / persistence state.
    tree.setProperty ("model",        "htdemucs", nullptr);
    tree.setProperty ("exportFormat", "wav24",    nullptr);
    tree.setProperty ("outputDir",    juce::File::getSpecialLocation (
                                          juce::File::userDocumentsDirectory)
                                          .getChildFile ("Stemmerizer Output")
                                          .getFullPathName(),    nullptr);

    // When a job finishes, hand the results to the in-memory session so the
    // editor can play / mix / drag them out.
    queue.setFinishedCallback ([this](const std::string& path,
                                      std::vector<float>&& original,
                                      int sr, int ch, long long frames,
                                      const dsp::SplitResult& result)
    {
        sess.loadFromResult (path, std::move (original), sr, ch, frames, result);

        // ---- Recent-splits bookkeeping ---------------------------------
        // Append this finished split to the recent list. We don't have the
        // outputDir / format / stem-paths in the FinishedCallback signature
        // this round — JobQueue's callback was designed around the playable
        // result, not the disk artefacts. Read them out of the ValueTree
        // instead (same source the job was constructed from). They MAY have
        // drifted between split start and finish (user changed selector
        // mid-run); for v1 we accept that, ship the feature, and revisit.
        //
        // TODO: pass outputDir + exportFormat + the list of written stem
        // paths through JobQueue::FinishedCallback so we don't have to
        // reconstruct them here and so concurrent format/folder changes
        // can't desync the recent entry from what's actually on disk.
        if (result.success && ! result.stems.empty())
        {
            dsp::RecentEntry entry;

            const auto outputDir = tree.getProperty ("outputDir").toString();
            const auto format    = tree.getProperty ("exportFormat").toString();
            const auto ext       = extensionForFormat (format);

            std::string basenameStd;
            try { basenameStd = std::filesystem::path (path).stem().string(); }
            catch (...) { basenameStd.clear(); }
            const juce::String basename (basenameStd);

            const auto nowMs = juce::Time::getCurrentTime().toMilliseconds();
            static std::atomic<int> counter { 0 };
            entry.id            = juce::String (nowMs) + "-"
                                + juce::String (counter.fetch_add (1));
            entry.inputPath     = juce::String (path);
            entry.inputBasename = basename;
            entry.outputDir     = outputDir;
            entry.timestamp     = nowMs;
            entry.format        = format.isNotEmpty() ? format : juce::String ("wav24");

            // Reconstruct stem-on-disk paths using the SAME layout the
            // worker writes them in (see JobQueue.cpp ~L322): "<outputDir>/
            // <basename> - <stemName><ext>".
            const juce::File outDir (outputDir);
            for (const auto& s : result.stems)
            {
                const juce::String stemName (s.name);
                entry.stemNames.add (stemName);
                entry.stemFiles.add (
                    outDir.getChildFile (basename + " - " + stemName + ext)
                          .getFullPathName());
            }

            recents.add (std::move (entry));
        }
        // ---------------------------------------------------------------

        tport.prepare (frames, sr);
    });

    dsp::DragExporter::purgeOldScratchDirs();
}

StemmerizerProcessor::~StemmerizerProcessor() = default;

void StemmerizerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    maxBlockSize   = samplesPerBlock;
    scratchL.assign ((size_t) samplesPerBlock, 0.f);
    scratchR.assign ((size_t) samplesPerBlock, 0.f);
}

void StemmerizerProcessor::releaseResources()
{
    scratchL.clear(); scratchL.shrink_to_fit();
    scratchR.clear(); scratchR.shrink_to_fit();
}

bool StemmerizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() &&
        mainOut != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == mainOut;
}

void StemmerizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nodenorm;

    const int numFrames = buffer.getNumSamples();
    const int numCh     = buffer.getNumChannels();

    // Always start from silence — Stemmerizer is a "generator" from the
    // host's perspective when there's a session loaded; without a session
    // it emits silence (we don't pass host audio through — that would
    // leak the input mix when no stems are available).
    buffer.clear();

    if (numFrames <= 0 || numCh <= 0) return;

    // Lock-free atomic load of the current snapshot — buffers stay alive
    // for the duration of this block thanks to the shared_ptr we hold.
    auto snap = sess.currentSnapshot();
    if (! snap || snap->numFrames <= 0 || snap->stems.empty()) return;

    // Sample-rate adaptation: session is at 44.1k (or whatever the file
    // was), host can be at 48k/96k/etc. The renderer reads from the
    // source buffer with linear interpolation, stepping by srcPerOut
    // samples per output frame.
    const double srcPerOut = (double) snap->sampleRate
                           / std::max (1.0, hostSampleRate);

    // Use the prepareToPlay-resident scratch buffers. If somehow the host
    // calls processBlock with a larger block than declared, fall back to
    // resizing on this call (rare; not the steady-state path).
    if ((int) scratchL.size() < numFrames)
    {
        scratchL.resize ((size_t) numFrames);
        scratchR.resize ((size_t) numFrames);
    }

    dsp::MixRenderer::render (*snap, sess.mixState(), tport,
                              sess.playOriginal(),
                              srcPerOut,
                              scratchL.data(), scratchR.data(), numFrames);

    if (numCh >= 2)
    {
        std::copy_n (scratchL.data(), numFrames, buffer.getWritePointer (0));
        std::copy_n (scratchR.data(), numFrames, buffer.getWritePointer (1));
        for (int c = 2; c < numCh; ++c) buffer.clear (c, 0, numFrames);
    }
    else
    {
        // Mono: average L+R.
        auto* dst = buffer.getWritePointer (0);
        for (int i = 0; i < numFrames; ++i)
            dst[i] = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
    }
}

juce::AudioProcessorEditor* StemmerizerProcessor::createEditor()
{
    return new StemmerizerEditor (*this);
}

void StemmerizerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream s (destData, false);
    tree.writeToStream (s);
}

void StemmerizerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto t = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (t.isValid()) tree = t;
}

juce::File StemmerizerProcessor::resolveWeightsDir() const
{
    // 1. Explicit override.
    if (auto env = juce::SystemStats::getEnvironmentVariable ("STEMMERIZER_WEIGHTS_DIR", {});
        env.isNotEmpty())
    {
        const juce::File f (env);
        if (f.isDirectory()) return f;
    }

    // 2. Per-user installed location (%APPDATA%\Stemmerizer\weights\).
    {
        auto userDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile ("Stemmerizer").getChildFile ("weights");
        if (userDir.isDirectory()) return userDir;
    }

    // 3. System-wide installed location (%PROGRAMDATA%\Stemmerizer\weights\).
    {
        auto sysDir = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                          .getChildFile ("Stemmerizer").getChildFile ("weights");
        if (sysDir.isDirectory()) return sysDir;
    }

    // 4. Inside the VST3 bundle (when shipped with the installer).
    //    For plugins, currentApplicationFile is the .vst3 bundle; for the
    //    standalone it's the .exe. currentExecutableFile is the HOST exe
    //    when we're loaded as a plugin, so it's only useful for the
    //    standalone fallback below.
    {
        const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
        // <bundle>/Contents/Resources/weights/   (the conventional spot)
        auto bundleRes = app.getChildFile ("Contents").getChildFile ("Resources").getChildFile ("weights");
        if (bundleRes.isDirectory()) return bundleRes;
        // Sibling of the bundle — works when both VST3 and weights/ live
        // in the same install dir.
        auto sibling = app.getParentDirectory().getChildFile ("weights");
        if (sibling.isDirectory()) return sibling;
    }

    // 5. Dev fallback — walk up from BOTH possible "self" anchors so we
    //    find resources/weights/ whether we're a plugin or standalone.
    for (auto anchor : { juce::File::currentExecutableFile,
                         juce::File::currentApplicationFile })
    {
        auto dev = juce::File::getSpecialLocation (anchor);
        for (int up = 0; up < 8 && dev.exists(); ++up)
        {
            const auto candidate = dev.getChildFile ("resources").getChildFile ("weights");
            if (candidate.isDirectory()) return candidate;
            dev = dev.getParentDirectory();
        }
    }

    return juce::File();
}

} // namespace stemmerizer

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new stemmerizer::StemmerizerProcessor();
}
