/*
    EspressoEngine — headless audio engine process for EspressoStudio.

    Phase 0 spike: opens the audio device (JACK/PipeWire preferred), creates a
    single-track Edit, and exposes a newline-delimited JSON control protocol on
    localhost TCP so the Electron UI can drive transport and read meters.

    Commands in:   {"cmd":"play"} {"cmd":"stop"} {"cmd":"record"} {"cmd":"rewind"} {"cmd":"quit"}
    Events out:    {"event":"hello",...} on connect, {"event":"state",...} at ~30 Hz,
                   {"event":"takes",...} on connect and after each recording
*/

#include <JuceHeader.h>

#include <sys/resource.h>

namespace te = tracktion;

static constexpr int kControlPort = 7177;

//==============================================================================
class ControlServer : private juce::Thread
{
public:
    ControlServer() : juce::Thread ("espresso-control") {}

    ~ControlServer() override
    {
        server.close();

        {
            const juce::ScopedLock l (lock);
            if (client != nullptr)
                client->close();
        }

        stopThread (3000);
    }

    std::function<void (juce::var)> onCommand;  // both called on the message thread
    std::function<void()> onConnect;

    bool start()
    {
        if (! server.createListener (kControlPort, "127.0.0.1"))
            return false;

        startThread();
        return true;
    }

    void broadcast (const juce::String& line)
    {
        const juce::ScopedLock l (lock);
        if (client != nullptr)
            client->write (line.toRawUTF8(), (int) line.getNumBytesAsUTF8());
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> c (server.waitForNextConnection());

            if (c == nullptr)
                continue;

            {
                const juce::ScopedLock l (lock);
                client = c.get();
            }

            if (onConnect)
                juce::MessageManager::callAsync ([this] { if (onConnect) onConnect(); });

            readLoop (*c);

            {
                const juce::ScopedLock l (lock);
                client = nullptr;
            }
        }
    }

    void readLoop (juce::StreamingSocket& c)
    {
        juce::String pending;
        char buffer[1024];

        while (! threadShouldExit())
        {
            const int ready = c.waitUntilReady (true, 200);

            if (ready < 0)
                break;
            if (ready == 0)
                continue;

            const int n = c.read (buffer, (int) sizeof (buffer) - 1, false);

            if (n <= 0)
                break;

            pending += juce::String::fromUTF8 (buffer, n);

            for (;;)
            {
                const int nl = pending.indexOf ("\n");
                if (nl < 0)
                    break;

                auto line = pending.substring (0, nl).trim();
                pending = pending.substring (nl + 1);

                if (line.isEmpty())
                    continue;

                auto parsed = juce::JSON::parse (line);

                if (onCommand)
                    juce::MessageManager::callAsync ([this, parsed] { if (onCommand) onCommand (parsed); });
            }
        }
    }

    juce::StreamingSocket server;
    juce::StreamingSocket* client = nullptr;  // owned by run(), guarded by lock
    juce::CriticalSection lock;
};

//==============================================================================
// rtkit promotes our audio threads with a hard ~200ms RLIMIT_RTTIME budget;
// overrunning it is a silent kernel SIGKILL. So: no spin-waiting, and render
// the graph on the audio thread alone until proper realtime limits
// (limits.d rtprio) make spinning safe.
struct EspressoEngineBehaviour : public te::EngineBehaviour
{
    int getNumberOfCPUsToUseForAudio() override   { return 1; }
};

//==============================================================================
// Diagnostic: proves whether the device layer is delivering audio callbacks.
struct CallbackCounter : public juce::AudioIODeviceCallback
{
    std::atomic<long> count { 0 };

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override   { ++count; }
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
};

//==============================================================================
class EspressoApp : private juce::Timer
{
public:
    bool initialise()
    {
        te::EditPlaybackContext::setThreadPoolStrategy (
            static_cast<int> (tracktion::graph::ThreadPoolStrategy::conditionVariable));
        auto& dm = engine.getDeviceManager();

        // Prefer JUCE's JACK device type: on a PipeWire system this connects
        // through pipewire-jack and gives us the pro-audio quantum.
        for (auto* type : dm.deviceManager.getAvailableDeviceTypes())
        {
            if (type->getTypeName() == "JACK")
            {
                dm.deviceManager.setCurrentAudioDeviceType ("JACK", true);
                break;
            }
        }

        dm.initialise (2, 2);

        if (dm.deviceManager.getCurrentAudioDevice() == nullptr)
        {
            std::cerr << "No audio device could be opened.\n";
            return false;
        }

        // PipeWire's rt module acquires realtime priority through rtkit, which
        // caps RLIMIT_RTTIME at ~200ms of RT CPU — exceeding it is a silent
        // kernel SIGKILL. The graph render can busy-wait past that, so lift
        // the cap now that the promotion has already happened.
        {
            const rlimit unlimited { RLIM_INFINITY, RLIM_INFINITY };
            setrlimit (RLIMIT_RTTIME, &unlimited);
        }

        // The wave device list is rebuilt asynchronously after the device
        // opens — wait for the inputs to appear before wiring up the edit.
        for (auto deadline = juce::Time::getMillisecondCounter() + 3000;
             dm.getNumWaveInDevices() == 0 && juce::Time::getMillisecondCounter() < deadline;)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        openEdit();

        engine.getDeviceManager().deviceManager.addAudioCallback (&callbackCounter);

        server.onCommand = [this] (juce::var v) { handleCommand (v); };
        server.onConnect = [this] { sendHello(); };

        if (! server.start())
        {
            std::cerr << "Could not listen on port " << kControlPort << " (already in use?)\n";
            return false;
        }

        logDeviceInfo();
        startTimerHz (30);
        return true;
    }

private:
    void openEdit()
    {
        auto sessionDir = juce::File::getCurrentWorkingDirectory().getChildFile ("spike-session");
        sessionDir.createDirectory();
        auto editFile = sessionDir.getChildFile ("EspressoSpike.tracktionedit");

        edit = editFile.existsAsFile() ? te::loadEditFromFile (engine, editFile)
                                       : te::createEmptyEdit (engine, editFile);
        edit->playInStopEnabled = true;

        auto& dm = engine.getDeviceManager();

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto* wip = dm.getWaveInDevice (i))
                wip->setStereoPair (false);

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        {
            if (auto* wip = dm.getWaveInDevice (i))
            {
                // PipeWire exposes the outputs back as monitor_* capture
                // ports — recording those would create a feedback loop.
                const bool isMonitorPort = wip->getName().containsIgnoreCase ("monitor");
                wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                wip->setEnabled (! isMonitorPort);
            }
        }

        edit->getTransport().ensureContextAllocated();

        // Route each mono wave input onto its own track, armed and ready.
        // Spike: cap at 2 — some fallback devices report hundreds of channels.
        constexpr int maxArmedTracks = 2;
        int trackNum = 0;
        for (auto instance : edit->getAllInputDevices())
        {
            if (trackNum >= maxArmedTracks)
                break;

            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice
                 && instance->getInputDevice().isEnabled())
            {
                edit->ensureNumberOfAudioTracks (trackNum + 1);

                if (auto t = te::getAudioTracks (*edit)[trackNum])
                {
                    [[maybe_unused]] auto result = instance->setTarget (t->itemID, true, &edit->getUndoManager(), 0);
                    instance->setRecordingEnabled (t->itemID, true);
                    ++trackNum;
                }
            }
        }

        edit->ensureNumberOfAudioTracks (1);

        if (auto t = te::getAudioTracks (*edit)[0])
            if (auto* m = t->getLevelMeterPlugin())
                m->measurer.addClient (meterClient);

        edit->restartPlayback();
    }

    void handleCommand (const juce::var& v)
    {
        const auto cmd = v.getProperty ("cmd", {}).toString();
        auto& transport = edit->getTransport();

        if (cmd == "play")
        {
            transport.play (false);
        }
        else if (cmd == "stop")
        {
            const bool wasRecording = transport.isRecording();
            transport.stop (false, false);

            if (wasRecording)
            {
                te::EditFileOperations (*edit).save (true, true, false);
                sendTakesInfo();
            }
        }
        else if (cmd == "record")
        {
            if (transport.isRecording())
            {
                transport.stop (false, false);
                te::EditFileOperations (*edit).save (true, true, false);
                sendTakesInfo();
            }
            else
            {
                transport.record (false);
            }
        }
        else if (cmd == "rewind")
        {
            transport.setPosition (te::TimePosition());
        }
        else if (cmd == "status")
        {
            sendHello();
        }
        else if (cmd == "quit")
        {
            te::EditFileOperations (*edit).save (true, true, false);
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }

    void sendHello()
    {
        auto& dm = engine.getDeviceManager();
        auto* dev = dm.deviceManager.getCurrentAudioDevice();

        if (dev == nullptr)
            return;

        auto* o = new juce::DynamicObject();
        o->setProperty ("event", "hello");
        o->setProperty ("device", dev->getTypeName() + ": " + dev->getName());
        o->setProperty ("sampleRate", dev->getCurrentSampleRate());
        o->setProperty ("bufferSize", dev->getCurrentBufferSizeSamples());
        auto latencySecs = dm.getOutputLatencySeconds();
        if (latencySecs <= 0.0)  // pipewire-jack reports 0; estimate one period
            latencySecs = dev->getCurrentBufferSizeSamples() / dev->getCurrentSampleRate();
        o->setProperty ("latencyMs", latencySecs * 1000.0);
        server.broadcast (juce::JSON::toString (juce::var (o), true) + "\n");

        sendTakesInfo();
    }

    void sendTakesInfo()
    {
        juce::Array<juce::var> tracks;

        for (auto t : te::getAudioTracks (*edit))
        {
            auto clips = t->getClips();

            if (clips.isEmpty())
                continue;

            double length = 0;
            for (auto* c : clips)
                length = std::max (length, c->getPosition().getEnd().inSeconds());

            auto* to = new juce::DynamicObject();
            to->setProperty ("track", t->getName());
            to->setProperty ("clips", clips.size());
            to->setProperty ("length", length);
            tracks.add (juce::var (to));
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("event", "takes");
        o->setProperty ("tracks", tracks);
        server.broadcast (juce::JSON::toString (juce::var (o), true) + "\n");
    }

    void timerCallback() override
    {
        auto& transport = edit->getTransport();

        const auto dbL = meterClient.getAndClearAudioLevel (0).dB;
        const auto dbR = meterClient.getAndClearAudioLevel (1).dB;

        auto* o = new juce::DynamicObject();
        o->setProperty ("event", "state");
        o->setProperty ("playing", transport.isPlaying());
        o->setProperty ("recording", transport.isRecording());
        o->setProperty ("position", transport.getPosition().inSeconds());
        o->setProperty ("levelL", juce::Decibels::decibelsToGain (dbL, -100.0f));
        o->setProperty ("levelR", juce::Decibels::decibelsToGain (dbR, -100.0f));
        o->setProperty ("callbacks", (juce::int64) callbackCounter.count.load());
        server.broadcast (juce::JSON::toString (juce::var (o), true) + "\n");
    }

    void logDeviceInfo()
    {
        auto& dm = engine.getDeviceManager();

        if (auto* dev = dm.deviceManager.getCurrentAudioDevice())
        {
            std::cout << "device: " << dev->getTypeName() << ": " << dev->getName() << "\n"
                      << "sample rate: " << dev->getCurrentSampleRate() << " Hz\n"
                      << "buffer: " << dev->getCurrentBufferSizeSamples() << " samples\n"
                      << "output latency: " << dm.getOutputLatencySeconds() * 1000.0 << " ms\n"
                      << "inputs: " << dm.getNumWaveInDevices() << " wave-in device(s)\n"
                      << "listening on 127.0.0.1:" << kControlPort << "\n"
                      << std::flush;
        }
    }

    te::Engine engine { std::make_unique<te::PropertyStorage> ("EspressoStudio"),
                        nullptr,
                        std::make_unique<EspressoEngineBehaviour>() };
    std::unique_ptr<te::Edit> edit;
    te::LevelMeasurer::Client meterClient;
    CallbackCounter callbackCounter;
    ControlServer server;
};

//==============================================================================
int main()
{
    // Ask PipeWire for a pro-audio quantum (overridable from the environment).
    setenv ("PIPEWIRE_LATENCY", "128/48000", /*overwrite*/ 0);

    juce::ScopedJuceInitialiser_GUI juceInit;

    EspressoApp app;

    if (! app.initialise())
        return 1;

    juce::MessageManager::getInstance()->runDispatchLoop();
    return 0;
}
