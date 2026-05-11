#include "AudioFileIO.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <new>

namespace stemmerizer::dsp::AudioFileIO
{

namespace
{
    juce::AudioFormatManager& fmtManager()
    {
        // C++11 magic-static initialisation is thread-safe; do the
        // registerBasicFormats() inside an IIFE that runs once with the
        // static itself. (Previously a check-then-set bool here was a
        // textbook data race even though only the worker called it.)
        static juce::AudioFormatManager& instance = []() -> juce::AudioFormatManager&
        {
            static juce::AudioFormatManager m;
            m.registerBasicFormats();   // WAV, AIFF, FLAC, OGG, MP3 (read-only)
            return m;
        }();
        return instance;
    }

    // Sanity cap on declared file length to defend against malformed
    // headers that claim absurd durations. JUCE's AudioFormatReader takes
    // lengthInSamples straight from the file's metadata, so a corrupt
    // FLAC declaring "10 hours at 192 kHz × 8 ch" would otherwise trigger
    // a ~50 GB std::vector::assign — instant bad_alloc that the editor
    // can catch but with a generic error. Cap = ~3 hr stereo @ 192 kHz
    // (way past any real-world plugin use case but well clear of the
    // bytes / size_t threshold).
    constexpr long long kMaxDecodeFrames = 3LL * 60 * 60 * 192000;  // 3 h @ 192 kHz

    // 4 GB safety net on the resulting interleaved buffer regardless of
    // sample rate / channel count. Even a "legit" 90 min × 192 kHz × 8 ch
    // file is over 5 GB, which we still don't want to silently accept in
    // a VST3 hosted process.
    constexpr long long kMaxDecodeBytes  = 4LL * 1024 * 1024 * 1024;
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

    // Length sanity checks (audit N7). lengthInSamples comes verbatim
    // from the file header, so a malformed file can ask us to allocate
    // tens of GB. Reject early with a meaningful error so the editor's
    // alert dialog can tell the user the file looks corrupted instead
    // of surfacing a bare bad_alloc.
    if (out.numFrames <= 0)
    {
        errorOut = "Audio file has zero frames (header may be corrupt).";
        return false;
    }
    if (out.numFrames > kMaxDecodeFrames)
    {
        errorOut = "Audio file is unreasonably long (header may be corrupt).";
        return false;
    }
    const long long totalSamples = out.numFrames * out.numChannels;
    if (totalSamples > (long long) (kMaxDecodeBytes / (long long) sizeof (float)))
    {
        errorOut = "Audio file too large to load into memory.";
        return false;
    }

    try
    {
        out.interleaved.assign ((size_t) totalSamples, 0.f);
    }
    catch (const std::bad_alloc&)
    {
        // Last line of defence — even within our cap, the host process
        // may be too fragmented to satisfy the allocation. Better to
        // surface this than crash the DAW.
        errorOut = "Out of memory loading audio.";
        return false;
    }

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
