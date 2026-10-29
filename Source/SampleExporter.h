#pragma once
// Export WAV (16 ou 24 bits, mono ou stéréo) compatible FL Studio.
#include <juce_audio_formats/juce_audio_formats.h>

namespace TF
{
inline juce::String safeFileName (juce::String s)
{
    s = s.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.()");
    s = s.trim().replaceCharacter (' ', '_');
    return s.isEmpty() ? juce::String ("TrapForge") : s;
}

// Écrit réellement le fichier puis relit son en-tête pour vérifier.
inline bool exportWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, int bits)
{
    if (audio.getNumSamples() <= 0) return false;
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat wav;
    {
        auto stream = std::make_unique<juce::FileOutputStream> (file);
        if (stream->failedToOpen()) return false;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, (unsigned int) audio.getNumChannels(), bits == 16 ? 16 : 24, {}, 0));
        if (writer == nullptr) return false;
        stream.release();   // le writer possède désormais le flux
        if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) return false;
    }

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    return reader != nullptr && reader->lengthInSamples == audio.getNumSamples() && reader->numChannels == (unsigned int) audio.getNumChannels();
}
}
