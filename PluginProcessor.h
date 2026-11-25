#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace rdr
{
    constexpr int numPads = 16;
    constexpr int baseNote = 36;              // C1 dans Ableton (comme le Drum Rack)
    constexpr int maxVoices = 48;
    constexpr double maxSampleSeconds = 30.0; // on tronque les fichiers trop longs

    struct SampleData
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        juce::File file;
    };

    using SamplePtr = std::shared_ptr<const SampleData>;

    juce::String paramId (const char* prefix, int pad);
}

//==============================================================================
// Scanne (recursivement) un dossier et garde la liste des fichiers audio en cache
class SampleLibrary
{
public:
    juce::Array<juce::File> getFiles (const juce::File& folder);
    void invalidate (const juce::File& folder);
    static bool isAudioFile (const juce::File& f);

private:
    juce::CriticalSection lock;
    std::map<juce::String, juce::Array<juce::File>> cache;
};

//==============================================================================
class RandomDrumRackProcessor : public juce::AudioProcessor,
                                public juce::ChangeBroadcaster,
                                private juce::Timer,
                                private juce::AsyncUpdater
{
public:
    RandomDrumRackProcessor();
    ~RandomDrumRackProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Random Drum Rack"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // API appelee depuis l'interface (message thread)
    void randomizePad (int pad);          // tire un nouveau sample pour ce pad (meme s'il est verrouille)
    void randomizeAll();                  // tire un nouveau sample pour tous les pads non verrouilles
    void loadFileIntoPad (int pad, const juce::File& file);
    void clearPad (int pad);

    void setGlobalFolder (const juce::File& folder);
    juce::File getGlobalFolder() const;
    void setPadFolder (int pad, const juce::File& folder); // File() = utiliser le dossier global
    juce::File getPadFolder (int pad) const;
    void setAllPadFolders (const juce::File& folder);      // File() = tous les pads reviennent au dossier global

    void setPadLocked (int pad, bool locked);
    bool isPadLocked (int pad) const;

    rdr::SamplePtr getPadSample (int pad) const;
    void previewPad (int pad);
    juce::uint32 getTriggerCount (int pad) const;
    bool isBusy() const { return pendingJobs.load() > 0; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Pad
    {
        rdr::SamplePtr sample;            // protege par 'lock'
        mutable juce::SpinLock lock;
        juce::File folder;                // protege par folderLock
        std::atomic<bool> locked { false };
        std::atomic<int> generation { 0 };
        std::atomic<juce::uint32> triggers { 0 };
        std::atomic<bool> previewRequested { false };
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* tune = nullptr;
    };

    struct Voice
    {
        bool active = false;
        int pad = -1;
        rdr::SamplePtr sample;
        double pos = 0.0, increment = 1.0;
        float gainL = 1.0f, gainR = 1.0f;
        juce::uint32 age = 0;
    };

    void queueLoad (int pad, const juce::File& specificFile, const juce::File& folder, const juce::File& avoid);
    std::shared_ptr<rdr::SampleData> readSample (const juce::File& file);
    void swapSample (int pad, rdr::SamplePtr newSample);
    juce::File folderForPad (int pad) const;

    void startVoice (int pad, float velocity);
    void renderVoices (juce::AudioBuffer<float>& buffer, int start, int num);

    void timerCallback() override;
    void handleAsyncUpdate() override { randomizeAll(); }

    std::array<Pad, rdr::numPads> pads;
    std::array<Voice, rdr::maxVoices> voices;
    juce::uint32 voiceCounter = 0;
    double hostSampleRate = 44100.0;

    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* rndAllParam = nullptr;
    bool lastRndAll = false;

    juce::File globalFolder;
    mutable juce::CriticalSection folderLock;

    juce::AudioFormatManager formatManager;
    SampleLibrary library;
    juce::Random rng;                     // utilise uniquement par le thread de chargement

    // Les samples remplaces sont gardes ici et liberes plus tard sur le message thread,
    // pour ne jamais desallouer de memoire dans le thread audio.
    std::vector<rdr::SamplePtr> graveyard;
    juce::CriticalSection graveyardLock;

    std::atomic<int> pendingJobs { 0 };
    std::atomic<bool> shuttingDown { false };
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RandomDrumRackProcessor)
};
