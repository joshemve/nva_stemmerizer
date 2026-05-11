#include "AudioFileIO.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace stemmerizer::dsp::AudioFileIO
{

namespace
{
    juce::AudioFormatManager& fmtManager()
    {
        // Single static manager keeps registered formats alive for the
        // lifetime of the plugin process.
        static juce::AudioFormatManager m;
        static bool registered = false;
        if (! registered)
        {
            m.registerBasicFormats();   // WAV, AIFF, FLAC, OGG, MP3 (read-only)
            registered = true;
        }
        return m;
    }
}

bool decode (const std::string& path, DecodedAudio& out, std::string& errorOut)
{
    const juce::File f { juce::String (path.c_str()) };
    if (! f.existsAsFile())
    {
        errorOut = "File not found.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (
        fmtManager().createReaderFor (f));

    if (reader == nullptr)
    {
        errorOut = "Unsupported audio format.";
        return false;
    }

    out.sampleRate  = (int) reader->sampleRate;
    out.numChannels = (int) std::max<unsigned int> (1, reader->numChannels);
    out.numFrames   = (long long) reader->lengthInSamples;
    out.interleaved.assign ((size_t) (out.numFrames * out.numChannels), 0.f);

    juce::AudioBuffer<float> tmp (out.numChannels, (int) std::min<long long> (out.numFrames, 1 << 20));
    long long pos = 0;
    while (pos < out.numFrames)
    {
        const int chunk = (int) std::min<long long> ((long long) tmp.getNumSamples(),
                                                     out.numFrames - pos);
        if (! reader->read (&tmp, 0, chunk, pos, true, out.numChannels >= 2))
        {
            errorOut = "Read error at frame " + std::to_string (pos);
            return false;
        }
        for (int c = 0; c < out.numChannels; ++c)
        {
            const float* src = tmp.getReadPointer (c);
            for (int i = 0; i < chunk; ++i)
                out.interleaved[(size_t) ((pos + i) * out.numChannels + c)] = src[i];
        }
        pos += chunk;
    }
    return true;
}

bool encode (const std::string& path,
             ExportFormat format,
             const float* interleaved,
             long long numFrames,
             int numChannels,
             int sampleRate,
             std::string& errorOut)
{
    const juce::File outFile { juce::String (path.c_str()) };
    outFile.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream());
    if (stream == nullptr || ! stream->openedOk())
    {
        errorOut = "Could not open output file for writing.";
        return false;
    }

    int  bitDepth      = 24;
    std::unique_ptr<juce::AudioFormat> fmt;

    switch (format)
    {
        case ExportFormat::Wav24:  fmt = std::make_unique<juce::WavAudioFormat>();  bitDepth = 24; break;
        case ExportFormat::Wav16:  fmt = std::make_unique<juce::WavAudioFormat>();  bitDepth = 16; break;
        case ExportFormat::Wav32f: fmt = std::make_unique<juce::WavAudioFormat>();  bitDepth = 32; break;
        case ExportFormat::Flac:   fmt = std::make_unique<juce::FlacAudioFormat>(); bitDepth = 24; break;
        case ExportFormat::Mp3:
            errorOut = "MP3 export requires the optional LAME add-on; install Stemmerizer-MP3.";
            return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt->createWriterFor (stream.get(), (double) sampleRate, (unsigned int) numChannels,
                              bitDepth, {}, 0));
    if (writer == nullptr)
    {
        errorOut = "Could not create writer (unsupported configuration?).";
        return false;
    }
    stream.release();   // writer takes ownership

    juce::AudioBuffer<float> buf (numChannels, (int) std::min<long long> (numFrames, 1 << 16));
    long long pos = 0;
    while (pos < numFrames)
    {
        const int chunk = (int) std::min<long long> ((long long) buf.getNumSamples(),
                                                     numFrames - pos);
        for (int c = 0; c < numChannels; ++c)
        {
            float* dst = buf.getWritePointer (c);
            for (int i = 0; i < chunk; ++i)
                dst[i] = interleaved[(size_t) ((pos + i) * numChannels + c)];
        }
        if (! writer->writeFromAudioSampleBuffer (buf, 0, chunk))
        {
            errorOut = "Write error at frame " + std::to_string (pos);
            return false;
        }
        pos += chunk;
    }
    return true;
}

} // namespace stemmerizer::dsp::AudioFileIO
