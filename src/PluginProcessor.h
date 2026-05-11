#pragma once

#include "dsp/JobQueue.h"
#include "dsp/StemSession.h"
#include "dsp/Transport.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stemmerizer
{

/// The plugin processor. We're an offline file-loader by design (drag a
/// file in -> background-process -> stems live in memory on the
/// processor), but unlike most splitters we ALSO play the resulting stems
/// back through the host's audio out. processBlock() reads the active
/// session + mix state + transport and renders the requested stereo mix.
///
/// We do NOT register any host-visible AudioProcessorParameters because
/// the user's working unit is "the file" — automating gains across DAW
/// projects doesn't make sense for an offline file-loader. State is
/// persisted via the ValueTree saved in get/setStateInformation.
class StemmerizerProcessor : public juce::AudioProcessor
{
public:
    StemmerizerProcessor();
    ~StemmerizerProcessor() override;

    // AudioProcessor =========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override            { return JucePlugin_Name; }
    bool   acceptsMidi() const override                    { return false; }
    bool   producesMidi() const override                   { return false; }
    bool   isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }
    int    getNumPrograms() override                       { return 1; }
    int    getCurrentProgram() override                    { return 0; }
    void   setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override       { return {}; }
    void   changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Stemmerizer ============================================================
    dsp::JobQueue&     jobQueue()        { return queue;   }
    dsp::StemSession&  session()         { return sess;    }
    dsp::Transport&    transport()       { return tport;   }
    juce::ValueTree&   state()           { return tree;    }

    /// Resolves the directory containing the .gguf weight files.
    juce::File resolveWeightsDir() const;

private:
    juce::ValueTree     tree { "stemmerizer" };
    dsp::StemSession    sess;
    dsp::Transport      tport;
    dsp::JobQueue       queue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemmerizerProcessor)
};

} // namespace stemmerizer
