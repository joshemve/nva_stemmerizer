#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "dsp/MixRenderer.h"
#include "dsp/DragExporter.h"

namespace stemmerizer
{

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
        tport.prepare (frames, sr);
    });

    dsp::DragExporter::purgeOldScratchDirs();
}

StemmerizerProcessor::~StemmerizerProcessor() = default;

void StemmerizerProcessor::prepareToPlay (double, int) {}
void StemmerizerProcessor::releaseResources() {}

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

    // Always start from silence — Stemmerizer is a "generator" plugin from
    // the host's perspective when there's a session loaded; otherwise pass
    // through silence (NOT host audio — we'd risk leaking the input mix
    // when no stems are loaded).
    buffer.clear();

    if (numFrames <= 0 || numCh <= 0) return;

    auto snap = sess.currentSnapshot();
    if (! snap || snap->numFrames <= 0 || snap->stems.empty()) return;

    // Render into temp planes, then copy out to whatever channel layout the
    // host provided.
    juce::HeapBlock<float> tempL (numFrames);
    juce::HeapBlock<float> tempR (numFrames);

    dsp::MixRenderer::render (*snap, sess.mixState(), tport,
                              tempL.getData(), tempR.getData(), numFrames);

    if (numCh >= 2)
    {
        std::copy_n (tempL.getData(), numFrames, buffer.getWritePointer (0));
        std::copy_n (tempR.getData(), numFrames, buffer.getWritePointer (1));
        for (int c = 2; c < numCh; ++c) buffer.clear (c, 0, numFrames);
    }
    else
    {
        // Mono: average L+R.
        auto* dst = buffer.getWritePointer (0);
        for (int i = 0; i < numFrames; ++i)
            dst[i] = 0.5f * (tempL[i] + tempR[i]);
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
