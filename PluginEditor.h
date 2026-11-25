#pragma once

#include "PluginProcessor.h"

//==============================================================================
class PadComponent : public juce::Component,
                     public juce::SettableTooltipClient,
                     public juce::FileDragAndDropTarget
{
public:
    PadComponent (RandomDrumRackProcessor&, int index);
    ~PadComponent() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    void refresh();
    void tick();

private:
    void showPadMenu (juce::Component* target);
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& caption);

    RandomDrumRackProcessor& proc;
    const int index;

    juce::Label nameLabel;
    juce::TextButton rndButton { "RND" }, lockButton { "LOCK" }, folderButton { "DIR" };
    juce::Slider gainSlider, tuneSlider, panSlider;
    juce::Label gainCaption, tuneCaption, panCaption;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> gainAttach, tuneAttach, panAttach;

    std::unique_ptr<juce::FileChooser> chooser;
    float flash = 0.0f;
    juce::uint32 lastTrigger = 0;
    bool dragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadComponent)
};

//==============================================================================
class RandomDrumRackEditor : public juce::AudioProcessorEditor,
                             private juce::ChangeListener,
                             private juce::Timer
{
public:
    explicit RandomDrumRackEditor (RandomDrumRackProcessor&);
    ~RandomDrumRackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshAll(); }
    void timerCallback() override;
    void refreshAll();
    void chooseGlobalFolder();

    RandomDrumRackProcessor& proc;

    juce::Label titleLabel, folderLabel, statusLabel, masterLabel;
    juce::TextButton chooseFolderButton, randomAllButton;
    juce::Slider masterSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttach;

    juce::OwnedArray<PadComponent> padComponents;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RandomDrumRackEditor)
};
