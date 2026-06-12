/*
    EspressoEngine — headless audio engine process for EspressoStudio.

    Phase 0 spike: opens the audio device (JACK/PipeWire preferred), creates a
    single-track Edit, and exposes a newline-delimited JSON control protocol on
    localhost TCP so the Electron UI can drive transport and read meters.

    Commands in:   {"cmd":"play"} {"cmd":"stop"} {"cmd":"record"} {"cmd":"quit"}
    Events out:    {"event":"hello",...} on connect, {"event":"state",...} at ~30 Hz
*/

#include <JuceHeader.h>

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
class EspressoApp : private juce::Timer
{
public:
    bool initialise()
    {
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

        openEdit();

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
                wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                wip->setEnabled (true);
            }
        }

        edit->getTransport().ensureContextAllocated();

        // Route each mono wave input onto its own track, armed and ready.
        int trackNum = 0;
        for (auto instance : edit->getAllInputDevices())
        {
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
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
                te::EditFileOperations (*edit).save (true, true, false);
        }
        else if (cmd == "record")
        {
            if (transport.isRecording())
            {
                transport.stop (false, false);
                te::EditFileOperations (*edit).save (true, true, false);
            }
            else
            {
                transport.record (false);
            }
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
        o->setProperty ("latencyMs", dm.getOutputLatencySeconds() * 1000.0);
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

    te::Engine engine { "EspressoStudio" };
    std::unique_ptr<te::Edit> edit;
    te::LevelMeasurer::Client meterClient;
    ControlServer server;
};

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    EspressoApp app;

    if (! app.initialise())
        return 1;

    juce::MessageManager::getInstance()->runDispatchLoop();
    return 0;
}
