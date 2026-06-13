#include "NamAmpPlugin.h"

#include <filesystem>

// NAM core (Eigen-heavy) is confined to this translation unit.
#include "NAM/get_dsp.h"
#include "NAM/dsp.h"

namespace
{
    const juce::Identifier kModelPathID  ("namModelPath");
    const juce::Identifier kModelNameID  ("namModelName");
    const juce::Identifier kInputGainID  ("inputGain");
    const juce::Identifier kOutputGainID ("outputGain");
}

//==============================================================================
NamAmpPlugin::NamAmpPlugin (te::PluginCreationInfo info)
    : te::Plugin (info)
{
    auto* um = getUndoManager();

    modelPath.referTo (state, kModelPathID, um);
    modelName.referTo (state, kModelNameID, um);
    inputGainValue.referTo  (state, kInputGainID,  um, 0.0f);
    outputGainValue.referTo (state, kOutputGainID, um, 0.0f);

    juce::NormalisableRange<float> inRange { -12.0f, 24.0f };
    inRange.setSkewForCentre (0.0f);
    inputGainParam = addParam ("inputGain", TRANS ("Input"), inRange,
                               [] (float v)               { return juce::Decibels::toString (v); },
                               [] (const juce::String& s) { return s.getFloatValue(); });
    inputGainParam->attachToCurrentValue (inputGainValue);

    juce::NormalisableRange<float> outRange { -24.0f, 12.0f };
    outRange.setSkewForCentre (0.0f);
    outputGainParam = addParam ("outputGain", TRANS ("Output"), outRange,
                                [] (float v)               { return juce::Decibels::toString (v); },
                                [] (const juce::String& s) { return s.getFloatValue(); });
    outputGainParam->attachToCurrentValue (outputGainValue);

    loadModelFromState();
}

NamAmpPlugin::~NamAmpPlugin()
{
    notifyListenersOfDeletion();
    inputGainParam->detachFromCurrentValue();
    outputGainParam->detachFromCurrentValue();
}

const char* NamAmpPlugin::getPluginName()                { return NEEDS_TRANS ("Neural Amp"); }

//==============================================================================
bool NamAmpPlugin::loadModel (const juce::File& f)
{
    if (! f.existsAsFile())
        return false;

    // Setting the path property triggers valueTreePropertyChanged ->
    // loadModelFromState (synchronously), which performs the actual load.
    state.setProperty (kModelNameID, f.getFileNameWithoutExtension(), getUndoManager());
    state.setProperty (kModelPathID, f.getFullPathName(), getUndoManager());

    return modelLoaded.load();
}

juce::File NamAmpPlugin::getModelFile() const
{
    const auto p = modelPath.get();
    return p.isEmpty() ? juce::File() : juce::File (p);
}

juce::String NamAmpPlugin::getModelDisplayName() const   { return modelName.get(); }

//==============================================================================
void NamAmpPlugin::loadModelFromState()
{
    // Read straight from the tree, not the CachedValue: when this runs from
    // valueTreePropertyChanged the CachedValue's own listener may not have
    // updated yet, so modelPath.get() can still be stale.
    const auto path = state.getProperty (kModelPathID).toString();

    if (path.isEmpty())
    {
        const juce::SpinLock::ScopedLockType l (modelLock);
        stagedModel.reset();
        haveStaged = true;
        modelLoaded = false;
        return;
    }

    std::unique_ptr<nam::DSP> dsp;

    try
    {
        dsp = nam::get_dsp (std::filesystem::path (path.toStdString()));
    }
    catch (const std::exception& e)
    {
        std::cerr << "[nam] failed to load model '" << path << "': " << e.what() << "\n" << std::flush;
        modelLoaded = false;
        return;
    }

    if (dsp == nullptr)
    {
        modelLoaded = false;
        return;
    }

    // Prewarm here on the message thread — it allocates and is too expensive
    // to run on the audio thread.
    dsp->Reset (currentSampleRate, currentMaxBlock);

    {
        const juce::SpinLock::ScopedLockType l (modelLock);
        stagedModel = std::move (dsp);
        haveStaged = true;
    }

    modelLoaded = true;
}

//==============================================================================
juce::String NamAmpPlugin::getName() const               { return TRANS ("Neural Amp"); }
juce::String NamAmpPlugin::getShortName (int)            { return "Amp"; }
juce::String NamAmpPlugin::getPluginType()               { return xmlTypeName; }
juce::String NamAmpPlugin::getSelectableDescription()    { return TRANS ("Neural Amp Modeler"); }

int NamAmpPlugin::getNumOutputChannelsGivenInputs (int numInputChannels)
{
    return juce::jmax (1, numInputChannels);
}

void NamAmpPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    currentSampleRate = info.sampleRate;
    currentMaxBlock   = juce::jmax (1, info.blockSizeSamples);

    monoIn.assign  ((size_t) currentMaxBlock, 0.0);
    monoOut.assign ((size_t) currentMaxBlock, 0.0);

    // initialise() is never called concurrently with the audio thread, so we
    // can drain the staged model and (re)prewarm at the real sample rate here.
    {
        const juce::SpinLock::ScopedLockType l (modelLock);

        if (haveStaged)
        {
            activeModel = std::move (stagedModel);
            haveStaged = false;
        }
    }

    if (activeModel != nullptr)
        activeModel->Reset (currentSampleRate, currentMaxBlock);
}

void NamAmpPlugin::deinitialise() {}

void NamAmpPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr)
        return;

    if (haveStaged.load())
    {
        const juce::SpinLock::ScopedTryLockType l (modelLock);

        if (l.isLocked())
        {
            activeModel = std::move (stagedModel);
            haveStaged = false;
        }
    }

    if (activeModel == nullptr)
        return;  // no model loaded — pass the signal through untouched

    const int n     = fc.bufferNumSamples;
    const int start = fc.bufferStartSample;
    auto& buf       = *fc.destBuffer;
    const int numCh = buf.getNumChannels();

    if (n <= 0 || numCh <= 0)
        return;

    if ((int) monoIn.size() < n)
    {
        monoIn.resize  ((size_t) n, 0.0);
        monoOut.resize ((size_t) n, 0.0);
    }

    const double inGain  = juce::Decibels::decibelsToGain (inputGainParam->getCurrentValue());
    const double outGain = juce::Decibels::decibelsToGain (outputGainParam->getCurrentValue());

    // Downmix the (guitar) input to mono and apply the input drive.
    for (int i = 0; i < n; ++i)
    {
        double s = 0.0;

        for (int c = 0; c < numCh; ++c)
            s += buf.getSample (c, start + i);

        monoIn[(size_t) i] = (s / numCh) * inGain;
    }

    NAM_SAMPLE* in  = monoIn.data();
    NAM_SAMPLE* out = monoOut.data();
    activeModel->process (&in, &out, n);

    // Broadcast the mono amp output (with output level) to every channel.
    for (int c = 0; c < numCh; ++c)
    {
        auto* d = buf.getWritePointer (c, start);

        for (int i = 0; i < n; ++i)
            d[i] = (float) (monoOut[(size_t) i] * outGain);
    }
}

void NamAmpPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    te::copyPropertiesToCachedValues (v, modelPath, modelName, inputGainValue, outputGainValue);

    for (auto* p : getAutomatableParameters())
        p->updateFromAttachedValue();

    loadModelFromState();
}

void NamAmpPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
{
    if (v == state && id == kModelPathID)
        loadModelFromState();
    else
        te::Plugin::valueTreePropertyChanged (v, id);
}
