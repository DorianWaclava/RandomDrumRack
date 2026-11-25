#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::String rdr::paramId (const char* prefix, int pad)
{
    return juce::String (prefix) + juce::String (pad);
}

static juce::File fileFromPath (const juce::String& path)
{
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

//==============================================================================
bool SampleLibrary::isAudioFile (const juce::File& f)
{
    if (f.getFileName().startsWith ("._")) // fichiers fantomes macOS
        return false;

    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

juce::Array<juce::File> SampleLibrary::getFiles (const juce::File& folder)
{
    const auto key = folder.getFullPathName();

    {
        const juce::ScopedLock sl (lock);
        auto it = cache.find (key);
        if (it != cache.end())
            return it->second;
    }

    juce::Array<juce::File> found;

    for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*",
                                                            juce::File::findFiles | juce::File::ignoreHiddenFiles))
    {
        const auto f = entry.getFile();
        if (isAudioFile (f))
            found.add (f);
    }

    const juce::ScopedLock sl (lock);
    cache[key] = found;
    return found;
}

void SampleLibrary::invalidate (const juce::File& folder)
{
    const juce::ScopedLock sl (lock);
    cache.erase (folder.getFullPathName());
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout RandomDrumRackProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "master", 1 }, "Master",
                                                             juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f));

    // Parametre automatisable / mappable MIDI : passer de OFF a ON = "tout randomiser"
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "rndAll", 1 }, "Randomize All", false));

    for (int i = 0; i < rdr::numPads; ++i)
    {
        const auto name = "Pad " + juce::String (i + 1);

        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rdr::paramId ("gain", i), 1 }, name + " Volume",
                                                                 juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rdr::paramId ("pan", i), 1 }, name + " Pan",
                                                                 juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rdr::paramId ("tune", i), 1 }, name + " Pitch",
                                                                 juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    }

    return layout;
}

RandomDrumRackProcessor::RandomDrumRackProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    masterParam = apvts.getRawParameterValue ("master");
    rndAllParam = apvts.getRawParameterValue ("rndAll");

    for (int i = 0; i < rdr::numPads; ++i)
    {
        auto& p = pads[(size_t) i];
        p.gain = apvts.getRawParameterValue (rdr::paramId ("gain", i));
        p.pan  = apvts.getRawParameterValue (rdr::paramId ("pan", i));
        p.tune = apvts.getRawParameterValue (rdr::paramId ("tune", i));
    }

    startTimer (2000);
}

RandomDrumRackProcessor::~RandomDrumRackProcessor()
{
    shuttingDown = true;
    stopTimer();
    cancelPendingUpdate();
    pool.removeAllJobs (true, 10000);
}

//==============================================================================
void RandomDrumRackProcessor::prepareToPlay (double sampleRate, int)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (auto& v : voices)
    {
        v.active = false;
        v.sample.reset();
    }
}

bool RandomDrumRackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void RandomDrumRackProcessor::startVoice (int padIndex, float velocity)
{
    auto& pad = pads[(size_t) padIndex];

    rdr::SamplePtr s;
    {
        const juce::SpinLock::ScopedLockType sl (pad.lock);
        s = pad.sample;
    }

    if (s == nullptr || s->buffer.getNumSamples() < 2)
        return;

    Voice* v = nullptr;
    for (auto& cand : voices)
        if (! cand.active) { v = &cand; break; }

    if (v == nullptr) // plus de voix libre : on vole la plus ancienne
    {
        v = &voices[0];
        for (auto& cand : voices)
            if (cand.age < v->age)
                v = &cand;
    }

    const float gainDb = pad.gain->load();
    const float panV   = pad.pan->load();
    const float tune   = pad.tune->load();
    const float g      = juce::Decibels::decibelsToGain (gainDb, -48.0f) * velocity;

    v->sample    = std::move (s); // l'ancien sample n'est jamais libere ici (pad ou graveyard le retiennent)
    v->pad       = padIndex;
    v->pos       = 0.0;
    v->increment = (v->sample->sampleRate / hostSampleRate) * std::pow (2.0, (double) tune / 12.0);
    v->gainL     = g * (panV > 0.0f ? 1.0f - panV : 1.0f);
    v->gainR     = g * (panV < 0.0f ? 1.0f + panV : 1.0f);
    v->age       = ++voiceCounter;
    v->active    = true;

    pad.triggers.fetch_add (1, std::memory_order_relaxed);
}

void RandomDrumRackProcessor::renderVoices (juce::AudioBuffer<float>& buffer, int start, int num)
{
    if (num <= 0)
        return;

    float* outL = buffer.getWritePointer (0, start);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& b   = v.sample->buffer;
        const int len   = b.getNumSamples();
        const float* sl = b.getReadPointer (0);
        const float* sr = b.getNumChannels() > 1 ? b.getReadPointer (1) : sl;

        for (int i = 0; i < num; ++i)
        {
            const int i0 = (int) v.pos;
            if (i0 + 1 >= len)
            {
                v.active = false;
                break;
            }

            const float fr = (float) (v.pos - (double) i0);
            const float l  = sl[i0] + fr * (sl[i0 + 1] - sl[i0]);
            const float r  = sr[i0] + fr * (sr[i0 + 1] - sr[i0]);

            if (outR != nullptr)
            {
                outL[i] += l * v.gainL;
                outR[i] += r * v.gainR;
            }
            else
            {
                outL[i] += 0.5f * (l * v.gainL + r * v.gainR);
            }

            v.pos += v.increment;
        }

        if (! v.active)
            v.sample.reset();
    }
}

void RandomDrumRackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    // Automation "Randomize All" : front montant -> randomisation (faite sur le message thread)
    const bool rnd = rndAllParam->load() > 0.5f;
    if (rnd && ! lastRndAll)
        triggerAsyncUpdate();
    lastRndAll = rnd;

    // Pre-ecoute depuis l'interface (clic sur un pad)
    for (int i = 0; i < rdr::numPads; ++i)
        if (pads[(size_t) i].previewRequested.exchange (false))
            startVoice (i, 0.8f);

    int pos = 0;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int t = juce::jlimit (0, numSamples, metadata.samplePosition);

        renderVoices (buffer, pos, t - pos);
        pos = t;

        if (msg.isNoteOn())
        {
            const int padIndex = msg.getNoteNumber() - rdr::baseNote;
            if (padIndex >= 0 && padIndex < rdr::numPads)
                startVoice (padIndex, msg.getFloatVelocity());
        }
        else if (msg.isAllSoundOff())
        {
            for (auto& v : voices)
                if (v.active) { v.active = false; v.sample.reset(); }
        }
    }

    renderVoices (buffer, pos, numSamples - pos);

    buffer.applyGain (juce::Decibels::decibelsToGain (masterParam->load(), -48.0f));
}

//==============================================================================
std::shared_ptr<rdr::SampleData> RandomDrumRackProcessor::readSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return {};

    const auto maxLen = (juce::int64) (reader->sampleRate * rdr::maxSampleSeconds);
    const int len = (int) std::min (reader->lengthInSamples, maxLen);
    if (len <= 1)
        return {};

    auto data = std::make_shared<rdr::SampleData>();
    const int numCh = juce::jmin (2, (int) reader->numChannels);

    data->buffer.setSize (numCh, len);
    reader->read (&data->buffer, 0, len, 0, true, numCh > 1);
    data->sampleRate = reader->sampleRate;
    data->file = file;
    return data;
}

void RandomDrumRackProcessor::swapSample (int padIndex, rdr::SamplePtr newSample)
{
    auto& pad = pads[(size_t) padIndex];
    rdr::SamplePtr old;

    {
        const juce::SpinLock::ScopedLockType sl (pad.lock);
        old = std::move (pad.sample);
        pad.sample = std::move (newSample);
    }

    if (old != nullptr)
    {
        const juce::ScopedLock sl (graveyardLock);
        graveyard.push_back (std::move (old));
    }
}

void RandomDrumRackProcessor::queueLoad (int padIndex, const juce::File& specificFile,
                                         const juce::File& folder, const juce::File& avoid)
{
    const int gen = ++pads[(size_t) padIndex].generation;
    ++pendingJobs;
    sendChangeMessage();

    pool.addJob ([this, padIndex, specificFile, folder, avoid, gen]
    {
        auto& pad = pads[(size_t) padIndex];

        if (! shuttingDown)
        {
            auto file = specificFile;

            if (file == juce::File())
            {
                const auto files = library.getFiles (folder);

                if (files.size() == 1)
                    file = files.getFirst();
                else if (files.size() > 1)
                    do { file = files[rng.nextInt (files.size())]; } while (file == avoid); // jamais 2x le meme d'affilee
            }

            if (file.existsAsFile() && pad.generation.load() == gen)
                if (auto data = readSample (file))
                    if (pad.generation.load() == gen) // une demande plus recente a pu arriver entre temps
                        swapSample (padIndex, std::move (data));
        }

        --pendingJobs;
        sendChangeMessage();
    });
}

void RandomDrumRackProcessor::timerCallback()
{
    const juce::ScopedLock sl (graveyardLock);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const rdr::SamplePtr& p) { return p.use_count() <= 1; }),
                     graveyard.end());
}

//==============================================================================
juce::File RandomDrumRackProcessor::folderForPad (int padIndex) const
{
    const juce::ScopedLock sl (folderLock);
    const auto& pf = pads[(size_t) padIndex].folder;
    return pf.isDirectory() ? pf : globalFolder;
}

void RandomDrumRackProcessor::randomizePad (int padIndex)
{
    if (padIndex < 0 || padIndex >= rdr::numPads)
        return;

    const auto folder = folderForPad (padIndex);
    if (! folder.isDirectory())
        return;

    const auto current = getPadSample (padIndex);
    queueLoad (padIndex, {}, folder, current != nullptr ? current->file : juce::File());
}

void RandomDrumRackProcessor::randomizeAll()
{
    for (int i = 0; i < rdr::numPads; ++i)
        if (! isPadLocked (i))
            randomizePad (i);
}

void RandomDrumRackProcessor::loadFileIntoPad (int padIndex, const juce::File& file)
{
    if (padIndex >= 0 && padIndex < rdr::numPads && SampleLibrary::isAudioFile (file))
        queueLoad (padIndex, file, {}, {});
}

void RandomDrumRackProcessor::clearPad (int padIndex)
{
    ++pads[(size_t) padIndex].generation;
    swapSample (padIndex, nullptr);
    sendChangeMessage();
}

void RandomDrumRackProcessor::setGlobalFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (folderLock);
        globalFolder = folder;
    }

    library.invalidate (folder); // re-scan au prochain tirage

    // Les pads qui utilisent le dossier global (et non verrouilles) tirent directement dans le nouveau dossier
    for (int i = 0; i < rdr::numPads; ++i)
        if (! isPadLocked (i) && ! getPadFolder (i).isDirectory())
            randomizePad (i);

    sendChangeMessage();
}

juce::File RandomDrumRackProcessor::getGlobalFolder() const
{
    const juce::ScopedLock sl (folderLock);
    return globalFolder;
}

void RandomDrumRackProcessor::setPadFolder (int padIndex, const juce::File& folder)
{
    {
        const juce::ScopedLock sl (folderLock);
        pads[(size_t) padIndex].folder = folder;
    }

    if (folder.isDirectory())
    {
        library.invalidate (folder);
        randomizePad (padIndex);
    }

    sendChangeMessage();
}

void RandomDrumRackProcessor::setAllPadFolders (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (folderLock);
        for (auto& pad : pads)
            pad.folder = folder;
    }

    if (folder.isDirectory())
        library.invalidate (folder);

    randomizeAll(); // nouveau tirage sur les pads non verrouilles
    sendChangeMessage();
}

juce::File RandomDrumRackProcessor::getPadFolder (int padIndex) const
{
    const juce::ScopedLock sl (folderLock);
    return pads[(size_t) padIndex].folder;
}

void RandomDrumRackProcessor::setPadLocked (int padIndex, bool shouldLock)
{
    pads[(size_t) padIndex].locked = shouldLock;
    sendChangeMessage();
}

bool RandomDrumRackProcessor::isPadLocked (int padIndex) const
{
    return pads[(size_t) padIndex].locked.load();
}

rdr::SamplePtr RandomDrumRackProcessor::getPadSample (int padIndex) const
{
    const auto& pad = pads[(size_t) padIndex];
    const juce::SpinLock::ScopedLockType sl (pad.lock);
    return pad.sample;
}

void RandomDrumRackProcessor::previewPad (int padIndex)
{
    pads[(size_t) padIndex].previewRequested = true;
}

juce::uint32 RandomDrumRackProcessor::getTriggerCount (int padIndex) const
{
    return pads[(size_t) padIndex].triggers.load (std::memory_order_relaxed);
}

//==============================================================================
// Sauvegarde dans le projet Ableton : parametres + chemin des samples tires + dossiers + verrous
void RandomDrumRackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("RandomDrumRack");
    root.setProperty ("globalFolder", getGlobalFolder().getFullPathName(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree padsTree ("Pads");
    for (int i = 0; i < rdr::numPads; ++i)
    {
        juce::ValueTree p ("Pad");
        const auto s = getPadSample (i);
        p.setProperty ("index", i, nullptr);
        p.setProperty ("file", s != nullptr ? s->file.getFullPathName() : juce::String(), nullptr);
        p.setProperty ("folder", getPadFolder (i).getFullPathName(), nullptr);
        p.setProperty ("locked", isPadLocked (i), nullptr);
        padsTree.appendChild (p, nullptr);
    }
    root.appendChild (padsTree, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void RandomDrumRackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("RandomDrumRack"))
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    {
        const juce::ScopedLock sl (folderLock);
        globalFolder = fileFromPath (root.getProperty ("globalFolder").toString());
    }

    const auto padsTree = root.getChildWithName ("Pads");
    for (int c = 0; c < padsTree.getNumChildren(); ++c)
    {
        const auto p = padsTree.getChild (c);
        const int idx = (int) p.getProperty ("index", -1);
        if (idx < 0 || idx >= rdr::numPads)
            continue;

        {
            const juce::ScopedLock sl (folderLock);
            pads[(size_t) idx].folder = fileFromPath (p.getProperty ("folder").toString());
        }

        pads[(size_t) idx].locked = (bool) p.getProperty ("locked", false);

        const auto file = fileFromPath (p.getProperty ("file").toString());
        if (file.existsAsFile())
            queueLoad (idx, file, {}, {});
    }

    sendChangeMessage();
}

//==============================================================================
juce::AudioProcessorEditor* RandomDrumRackProcessor::createEditor()
{
    return new RandomDrumRackEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RandomDrumRackProcessor();
}
