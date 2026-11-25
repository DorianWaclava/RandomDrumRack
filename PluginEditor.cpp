#include "PluginEditor.h"

namespace
{
    juce::String fr (const char* utf8) { return juce::String::fromUTF8 (utf8); }
    juce::Font font (float size) { return juce::Font (juce::FontOptions (size)); }

    const juce::Colour bgColour     { 0xff17191e };
    const juce::Colour padColour    { 0xff262a33 };
    const juce::Colour borderColour { 0xff3a3f4b };
    const juce::Colour hitColour    { 0xffff8a3d };
    const juce::Colour lockColour   { 0xffe8c547 };
    const juce::Colour folderColour { 0xff4aa3ff };
}

//==============================================================================
PadComponent::PadComponent (RandomDrumRackProcessor& p, int i)
    : proc (p), index (i)
{
    nameLabel.setFont (font (14.0f));
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    rndButton.setTooltip (fr ("Tirer un nouveau sample au hasard pour ce pad"));
    rndButton.setColour (juce::TextButton::buttonColourId, hitColour.darker (0.4f));
    rndButton.onClick = [this] { proc.randomizePad (index); };
    addAndMakeVisible (rndButton);

    lockButton.setTooltip (fr ("Verrouiller : ce pad est ignoré par « Randomiser tout »"));
    lockButton.setClickingTogglesState (true);
    lockButton.setColour (juce::TextButton::buttonOnColourId, lockColour.darker (0.3f));
    lockButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lockButton.onClick = [this] { proc.setPadLocked (index, lockButton.getToggleState()); };
    addAndMakeVisible (lockButton);

    folderButton.setTooltip (fr ("Dossier propre à ce pad, charger un fichier précis, vider…"));
    folderButton.onClick = [this] { showPadMenu (&folderButton); };
    addAndMakeVisible (folderButton);

    setupKnob (gainSlider, gainCaption, "Vol");
    setupKnob (tuneSlider, tuneCaption, "Pitch");
    setupKnob (panSlider,  panCaption,  "Pan");

    gainAttach = std::make_unique<Attachment> (proc.apvts, rdr::paramId ("gain", index), gainSlider);
    tuneAttach = std::make_unique<Attachment> (proc.apvts, rdr::paramId ("tune", index), tuneSlider);
    panAttach  = std::make_unique<Attachment> (proc.apvts, rdr::paramId ("pan",  index), panSlider);

    lastTrigger = proc.getTriggerCount (index);
    refresh();
}

void PadComponent::setupKnob (juce::Slider& s, juce::Label& caption, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);
    s.setColour (juce::Slider::rotarySliderFillColourId, hitColour);
    addAndMakeVisible (s);

    caption.setText (text, juce::dontSendNotification);
    caption.setFont (font (11.0f));
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    caption.setInterceptsMouseClicks (false, false);
    caption.attachToComponent (&s, false);
}

void PadComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    auto base = padColour;
    if (dragOver)
        base = base.interpolatedWith (folderColour, 0.35f);
    base = base.interpolatedWith (hitColour, flash * 0.6f);

    g.setColour (base);
    g.fillRoundedRectangle (b, 6.0f);

    const bool locked = proc.isPadLocked (index);
    g.setColour (locked ? lockColour : borderColour);
    g.drawRoundedRectangle (b, 6.0f, locked ? 2.0f : 1.0f);

    auto header = getLocalBounds().reduced (8, 5).removeFromTop (14);
    g.setFont (font (11.0f));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawText (juce::MidiMessage::getMidiNoteName (rdr::baseNote + index, true, true, 3),
                header, juce::Justification::centredLeft);

    const auto padFolder = proc.getPadFolder (index);
    const auto right = padFolder.isDirectory() ? padFolder.getFileName() + "  |  Pad " + juce::String (index + 1)
                                               : "Pad " + juce::String (index + 1);
    g.setColour (padFolder.isDirectory() ? folderColour : juce::Colours::white.withAlpha (0.55f));
    g.drawText (right, header, juce::Justification::centredRight);
}

void PadComponent::resized()
{
    auto r = getLocalBounds().reduced (8, 5);
    r.removeFromTop (16);
    nameLabel.setBounds (r.removeFromTop (34));

    auto buttons = r.removeFromTop (22);
    const int w = buttons.getWidth() / 3;
    rndButton.setBounds (buttons.removeFromLeft (w).reduced (2, 0));
    lockButton.setBounds (buttons.removeFromLeft (w).reduced (2, 0));
    folderButton.setBounds (buttons.reduced (2, 0));

    r.removeFromTop (18); // place pour les legendes des knobs
    const int kw = r.getWidth() / 3;
    gainSlider.setBounds (r.removeFromLeft (kw));
    tuneSlider.setBounds (r.removeFromLeft (kw));
    panSlider.setBounds (r);
}

void PadComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showPadMenu (this);
    else
        proc.previewPad (index);
}

void PadComponent::showPadMenu (juce::Component* target)
{
    const auto padFolder = proc.getPadFolder (index);

    juce::PopupMenu m;
    m.addSectionHeader (padFolder.isDirectory() ? fr ("Dossier du pad : ") + padFolder.getFileName()
                                                : fr ("Ce pad utilise le dossier global"));
    m.addItem (1, fr ("Choisir un dossier pour ce pad…"));
    m.addItem (2, fr ("Revenir au dossier global"), padFolder != juce::File());
    m.addSeparator();
    m.addItem (6, fr ("Appliquer le dossier de ce pad à TOUS les pads"), padFolder.isDirectory());
    m.addItem (7, fr ("Remettre TOUS les pads sur le dossier global"));
    m.addSeparator();
    m.addItem (3, fr ("Charger un sample précis…"));
    m.addItem (4, fr ("Montrer le sample dans l'explorateur"), proc.getPadSample (index) != nullptr);
    m.addItem (5, fr ("Vider le pad"), proc.getPadSample (index) != nullptr);

    juce::Component::SafePointer<PadComponent> safe (this);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& self = *safe;
        const auto start = self.proc.getPadFolder (self.index).isDirectory() ? self.proc.getPadFolder (self.index)
                                                                              : self.proc.getGlobalFolder();

        if (result == 1)
        {
            self.chooser = std::make_unique<juce::FileChooser> (fr ("Dossier de samples pour le pad ") + juce::String (self.index + 1), start);
            self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                       [safe] (const juce::FileChooser& fc)
                                       {
                                           const auto f = fc.getResult();
                                           if (safe != nullptr && f.isDirectory())
                                               safe->proc.setPadFolder (safe->index, f);
                                       });
        }
        else if (result == 2)
        {
            self.proc.setPadFolder (self.index, {});
        }
        else if (result == 3)
        {
            self.chooser = std::make_unique<juce::FileChooser> (fr ("Choisir un sample"), start, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [safe] (const juce::FileChooser& fc)
                                       {
                                           const auto f = fc.getResult();
                                           if (safe != nullptr && f.existsAsFile())
                                               safe->proc.loadFileIntoPad (safe->index, f);
                                       });
        }
        else if (result == 4)
        {
            if (auto s = self.proc.getPadSample (self.index))
                s->file.revealToUser();
        }
        else if (result == 5)
        {
            self.proc.clearPad (self.index);
        }
        else if (result == 6)
        {
            self.proc.setAllPadFolders (self.proc.getPadFolder (self.index));
        }
        else if (result == 7)
        {
            self.proc.setAllPadFolders ({});
        }
    });
}

bool PadComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (SampleLibrary::isAudioFile (juce::File (f)) || juce::File (f).isDirectory())
            return true;
    return false;
}

void PadComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();

    if (files.isEmpty())
        return;

    const juce::File f (files[0]);
    if (f.isDirectory())
        proc.setPadFolder (index, f); // glisser un dossier = dossier propre a ce pad
    else
        proc.loadFileIntoPad (index, f);
}

void PadComponent::refresh()
{
    const auto s = proc.getPadSample (index);

    nameLabel.setText (s != nullptr ? s->file.getFileNameWithoutExtension() : fr ("(vide)"), juce::dontSendNotification);
    nameLabel.setColour (juce::Label::textColourId, s != nullptr ? juce::Colours::white : juce::Colours::white.withAlpha (0.35f));
    setTooltip (s != nullptr ? s->file.getFullPathName() : juce::String());

    lockButton.setToggleState (proc.isPadLocked (index), juce::dontSendNotification);

    const bool hasOwnFolder = proc.getPadFolder (index).isDirectory();
    folderButton.setColour (juce::TextButton::buttonColourId,
                            hasOwnFolder ? folderColour.darker (0.5f)
                                         : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    repaint();
}

void PadComponent::tick()
{
    const auto t = proc.getTriggerCount (index);

    if (t != lastTrigger)
    {
        lastTrigger = t;
        flash = 1.0f;
        repaint();
    }
    else if (flash > 0.0f)
    {
        flash = juce::jmax (0.0f, flash - 0.1f);
        repaint();
    }
}

//==============================================================================
RandomDrumRackEditor::RandomDrumRackEditor (RandomDrumRackProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    titleLabel.setText ("RANDOM DRUM RACK", juce::dontSendNotification);
    titleLabel.setFont (font (20.0f));
    titleLabel.setColour (juce::Label::textColourId, hitColour);
    addAndMakeVisible (titleLabel);

    chooseFolderButton.setButtonText (fr ("Dossier de samples…"));
    chooseFolderButton.setTooltip (fr ("Dossier scanné (sous-dossiers compris) pour tous les pads sans dossier propre"));
    chooseFolderButton.onClick = [this] { chooseGlobalFolder(); };
    addAndMakeVisible (chooseFolderButton);

    randomAllButton.setButtonText (fr ("RANDOMISER TOUT"));
    randomAllButton.setTooltip (fr ("Nouveau sample sur chaque pad non verrouillé"));
    randomAllButton.setColour (juce::TextButton::buttonColourId, hitColour.darker (0.2f));
    randomAllButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    randomAllButton.onClick = [this] { proc.randomizeAll(); };
    addAndMakeVisible (randomAllButton);

    folderLabel.setFont (font (13.0f));
    folderLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (folderLabel);

    statusLabel.setFont (font (13.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    masterSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    masterSlider.setColour (juce::Slider::trackColourId, hitColour);
    addAndMakeVisible (masterSlider);
    masterAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "master", masterSlider);

    masterLabel.setText ("Master", juce::dontSendNotification);
    masterLabel.setFont (font (13.0f));
    masterLabel.attachToComponent (&masterSlider, true);

    for (int i = 0; i < rdr::numPads; ++i)
        addAndMakeVisible (padComponents.add (new PadComponent (proc, i)));

    proc.addChangeListener (this);
    refreshAll();
    startTimerHz (30);

    setSize (900, 860);
}

RandomDrumRackEditor::~RandomDrumRackEditor()
{
    stopTimer();
    proc.removeChangeListener (this);
}

void RandomDrumRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);
}

void RandomDrumRackEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto top = r.removeFromTop (66);

    auto row1 = top.removeFromTop (32);
    titleLabel.setBounds (row1.removeFromLeft (260));
    randomAllButton.setBounds (row1.removeFromRight (190));
    row1.removeFromRight (16);
    masterSlider.setBounds (row1.removeFromRight (220).reduced (0, 4));

    auto row2 = top.reduced (0, 5);
    chooseFolderButton.setBounds (row2.removeFromLeft (190));
    statusLabel.setBounds (row2.removeFromRight (180));
    folderLabel.setBounds (row2.reduced (10, 0));

    r.removeFromTop (6);

    // Disposition facon Drum Rack : pad 1 (C1) en bas a gauche
    const int cw = r.getWidth() / 4;
    const int ch = r.getHeight() / 4;

    for (int i = 0; i < rdr::numPads; ++i)
    {
        const int col = i % 4;
        const int row = 3 - i / 4;
        padComponents[i]->setBounds (juce::Rectangle<int> (r.getX() + col * cw, r.getY() + row * ch, cw, ch).reduced (3));
    }
}

void RandomDrumRackEditor::timerCallback()
{
    for (auto* pc : padComponents)
        pc->tick();
}

void RandomDrumRackEditor::refreshAll()
{
    const auto folder = proc.getGlobalFolder();
    folderLabel.setText (folder.isDirectory() ? folder.getFullPathName() : fr ("Aucun dossier choisi — clique sur « Dossier de samples… »"),
                         juce::dontSendNotification);

    const bool busy = proc.isBusy();
    statusLabel.setText (busy ? fr ("Chargement…") : fr ("Prêt"), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, busy ? hitColour : juce::Colours::white.withAlpha (0.5f));

    for (auto* pc : padComponents)
        pc->refresh();
}

void RandomDrumRackEditor::chooseGlobalFolder()
{
    const auto current = proc.getGlobalFolder();
    chooser = std::make_unique<juce::FileChooser> (fr ("Choisis le dossier de samples"),
                                                   current.isDirectory() ? current
                                                                         : juce::File::getSpecialLocation (juce::File::userHomeDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.isDirectory())
                                  proc.setGlobalFolder (f);
                          });
}
