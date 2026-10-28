// nSynthRender: renders patches / performances to WAV for listening tests and offline analysis.
//   nSynthRender --patch <file.nsynthpatch> --out <file.wav> [--phrase auto|chords|bass|lead|pluck|keys|fx]
//   nSynthRender --perf <file.nsynthperf> --out <file.wav>
//   nSynthRender --factory --outdir <dir>     every embedded factory patch and performance
//   nSynthRender --all <Presets dir> --outdir <dir>
#include "PhraseRenderer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

using namespace nsynth;

namespace
{
bool writeWav (const juce::AudioBuffer<float>& audio, const juce::File& out)
{
    out.deleteFile();
    out.getParentDirectory().createDirectory();
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream().release());
    if (stream == nullptr)
        return false;
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

bool report (const juce::String& label, const juce::AudioBuffer<float>& audio, const juce::File& out)
{
    const auto loudness = tools::measure (audio);
    std::cout << label << "  peak " << juce::String (loudness.peakDb, 1) << " dBFS, loudest 400 ms "
              << juce::String (loudness.momentaryMaxDb, 1) << " dBFS" << std::endl;
    return writeWav (audio, out);
}

bool renderPatchFile (const juce::File& file, juce::String phrase, const juce::File& out)
{
    NSynthAudioProcessor processor;
    juce::String error;
    if (! processor.loadPatch (Layer::upper, file, &error))
    {
        std::cerr << file.getFullPathName() << ": " << error << std::endl;
        return false;
    }
    if (phrase.isEmpty() || phrase == "auto")
        phrase = tools::phraseForCategory (processor.getPatchCategory (Layer::upper) + " " + file.getParentDirectory().getFileName());
    return report (file.getFileName(), tools::renderPhrase (processor, phrase), out);
}

bool renderPerformanceFile (const juce::File& file, const juce::File& out)
{
    NSynthAudioProcessor processor;
    juce::String error;
    if (! processor.loadPerformance (file, &error))
    {
        std::cerr << file.getFullPathName() << ": " << error << std::endl;
        return false;
    }
    return report (file.getFileName(), tools::renderPhrase (processor, "chords"), out);
}

bool renderFactory (const juce::File& outDir)
{
    NSynthAudioProcessor scanner;
    auto& library = scanner.getPresetLibrary();
    bool ok = true;
    for (const auto& entry : library.getEntries())
    {
        if (! entry.factory)
            continue;
        NSynthAudioProcessor processor;
        juce::String error;
        const bool patch = entry.kind == nSynth::PresetKind::patch;
        if (! (patch ? processor.loadPatch (Layer::upper, entry, &error) : processor.loadPerformance (entry, &error)))
        {
            std::cerr << entry.name << ": " << error << std::endl;
            ok = false;
            continue;
        }
        const auto phrase = patch ? tools::phraseForCategory (entry.category) : juce::String ("chords");
        const auto stem = juce::File::createLegalFileName ((patch ? entry.category : juce::String ("Performance")) + "__" + entry.name)
                              .replaceCharacter (' ', '_');
        ok &= report (stem, tools::renderPhrase (processor, phrase), outDir.getChildFile (stem + ".wav"));
    }
    return ok;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);
    auto arg = [&] (const char* name) { const int i = args.indexOf (name); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    auto fileArg = [&] (const char* name) { return juce::File::getCurrentWorkingDirectory().getChildFile (arg (name)); };

    if (args.contains ("--factory"))
        return renderFactory (fileArg ("--outdir")) ? 0 : 1;
    if (arg ("--all").isNotEmpty())
    {
        const auto root = fileArg ("--all");
        const auto outDir = fileArg ("--outdir");
        bool ok = true;
        for (auto& f : root.findChildFiles (juce::File::findFiles, true, "*.nsynthpatch"))
            ok &= renderPatchFile (f, "auto", outDir.getChildFile (f.getParentDirectory().getFileName() + "__" + f.getFileNameWithoutExtension() + ".wav"));
        for (auto& f : root.findChildFiles (juce::File::findFiles, true, "*.nsynthperf"))
            ok &= renderPerformanceFile (f, outDir.getChildFile ("Performance__" + f.getFileNameWithoutExtension() + ".wav"));
        return ok ? 0 : 1;
    }
    if (arg ("--patch").isNotEmpty())
        return renderPatchFile (fileArg ("--patch"), arg ("--phrase"), fileArg ("--out")) ? 0 : 1;
    if (arg ("--perf").isNotEmpty())
        return renderPerformanceFile (fileArg ("--perf"), fileArg ("--out")) ? 0 : 1;

    std::cerr << "Usage: nSynthRender --patch <file> --out <wav> [--phrase auto|chords|bass|lead|pluck|keys|fx]\n"
                 "       nSynthRender --perf <file> --out <wav>\n"
                 "       nSynthRender --factory --outdir <dir>\n"
                 "       nSynthRender --all <Presets dir> --outdir <dir>" << std::endl;
    return 2;
}
