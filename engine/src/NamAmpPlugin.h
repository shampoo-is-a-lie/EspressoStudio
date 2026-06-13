/*
    NamAmpPlugin — Neural Amp Modeler as a first-class EspressoStudio effect.

    A Tracktion internal Plugin that wraps the MIT NeuralAmpModelerCore DSP
    (libs/NeuralAmpModelerCore). It loads a .nam capture and runs the guitar
    signal through it mono, with input drive and output level controls.

    This is the "amp" half of the guitar rig; the matching cabinet is
    Tracktion's built-in ImpulseResponsePlugin (an IR loader). Keeping both as
    internal Tracktion plugins gives us native routing, automation and
    persistence — the custom Logic-style UI lives in the app and drives them
    over the control protocol.

    The NAM headers (Eigen-heavy) are confined to NamAmpPlugin.cpp; this header
    only forward-declares nam::DSP so Main.cpp's translation unit stays light.
*/

#pragma once

#include <JuceHeader.h>

namespace nam { class DSP; }

namespace te = tracktion;

//==============================================================================
class NamAmpPlugin  : public te::Plugin
{
public:
    explicit NamAmpPlugin (te::PluginCreationInfo);
    ~NamAmpPlugin() override;

    static const char* getPluginName();
    static inline const char* xmlTypeName = "espressoNamAmp";

    //==============================================================================
    /** Loads a .nam capture. Stores the path in the plugin state (so it
        persists and reloads with the edit) and stages the model for the audio
        thread. Returns false if the file doesn't exist or fails to parse.
    */
    bool loadModel (const juce::File&);

    juce::File getModelFile() const;
    juce::String getModelDisplayName() const;
    bool hasModelLoaded() const noexcept    { return modelLoaded.load(); }

    //==============================================================================
    te::AutomatableParameter::Ptr inputGainParam;   /**< Drive into the model, dB. */
    te::AutomatableParameter::Ptr outputGainParam;  /**< Level after the model, dB. */

    //==============================================================================
    /** @internal */ juce::String getName() const override;
    /** @internal */ juce::String getShortName (int) override;
    /** @internal */ juce::String getPluginType() override;
    /** @internal */ juce::String getSelectableDescription() override;
    /** @internal */ juce::String getVendor() override         { return "EspressoStudio"; }
    /** @internal */ int getNumOutputChannelsGivenInputs (int) override;
    /** @internal */ void initialise (const te::PluginInitialisationInfo&) override;
    /** @internal */ void deinitialise() override;
    /** @internal */ void applyToBuffer (const te::PluginRenderContext&) override;
    /** @internal */ void restorePluginStateFromValueTree (const juce::ValueTree&) override;

private:
    void loadModelFromState();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    juce::CachedValue<juce::String> modelPath, modelName;
    juce::CachedValue<float> inputGainValue, outputGainValue;

    // The active model is owned and used by the audio thread. New models are
    // built + prewarmed on the message thread, parked in stagedModel under
    // modelLock, then swapped in by the audio thread via a non-blocking try-lock.
    std::unique_ptr<nam::DSP> activeModel;
    std::unique_ptr<nam::DSP> stagedModel;
    juce::SpinLock modelLock;
    std::atomic<bool> haveStaged { false };
    std::atomic<bool> modelLoaded { false };

    double currentSampleRate = 44100.0;
    int currentMaxBlock = 512;

    std::vector<double> monoIn, monoOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NamAmpPlugin)
};
