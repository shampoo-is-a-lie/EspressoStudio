/*
    EspressoEngine — headless audio engine process for EspressoStudio.

    Phase 0 spike: opens the audio device (JACK/PipeWire preferred), creates a
    single-track Edit, and exposes a newline-delimited JSON control protocol on
    localhost TCP so the Electron UI can drive transport and read meters.

    Protocol v1.
    Transport:  {"cmd":"play"} {"cmd":"stop"} {"cmd":"record"} {"cmd":"rewind"}
                {"cmd":"seek","seconds":N} {"cmd":"quit"}
    Tracks:     {"cmd":"addTrack"} {"cmd":"removeTrack","track":i}
                {"cmd":"arm","track":i,"on":b} {"cmd":"mute","track":i,"on":b}
                {"cmd":"solo","track":i,"on":b} {"cmd":"volume","track":i,"db":N}
                {"cmd":"pan","track":i,"value":-1..1}
    Data:       {"cmd":"peaks","track":i,"clip":j}
                {"cmd":"export","format":"wav"|"flac","bitDepth":16|24|32}
    Events out: hello (on connect), state (~30 Hz), tracks (on connect and
                after any change), peaks (on request), exported
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
            const juce::ScopedLock l (clientsLock);
            clients.clear();
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
        const juce::ScopedLock l (clientsLock);

        for (int i = clients.size(); --i >= 0;)
        {
            if (clients[i]->isThreadRunning())
                clients[i]->send (line);
            else
                clients.remove (i);
        }
    }

private:
    struct ClientConnection : public juce::Thread
    {
        ClientConnection (ControlServer& s, std::unique_ptr<juce::StreamingSocket> so)
            : juce::Thread ("espresso-client"), server (s), sock (std::move (so))
        {
            startThread();
        }

        ~ClientConnection() override
        {
            if (sock != nullptr)
                sock->close();

            stopThread (2000);
        }

        void send (const juce::String& line)
        {
            sock->write (line.toRawUTF8(), (int) line.getNumBytesAsUTF8());
        }

        void run() override
        {
            server.readLoop (*sock);
        }

        ControlServer& server;
        std::unique_ptr<juce::StreamingSocket> sock;
    };

    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> c (server.waitForNextConnection());

            if (c == nullptr)
                continue;

            std::cerr << "[ctl] client connected\n" << std::flush;

            {
                const juce::ScopedLock l (clientsLock);
                clients.add (new ClientConnection (*this, std::move (c)));
            }

            if (onConnect)
                juce::MessageManager::callAsync ([this] { if (onConnect) onConnect(); });
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
    juce::OwnedArray<ClientConnection> clients;  // guarded by clientsLock
    juce::CriticalSection clientsLock;
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

// Headless: render/export tasks must be pumped manually (the default
// UIBehaviour's runTaskWithProgressBar is a no-op).
struct EspressoUIBehaviour : public te::UIBehaviour
{
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& t) override
    {
        while (t.runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
        {}
    }
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
        auto sessionDir = juce::File::getCurrentWorkingDirectory().getChildFile ("session");
        sessionDir.createDirectory();
        auto editFile = sessionDir.getChildFile ("EspressoProject.tracktionedit");

        edit = editFile.existsAsFile() ? te::loadEditFromFile (engine, editFile)
                                       : te::createEmptyEdit (engine, editFile);
        edit->playInStopEnabled = true;

        auto& dm = engine.getDeviceManager();

        // Pair the first two capture channels into one stereo input device.
        // PipeWire exposes the outputs back as monitor_* capture ports —
        // recording those would create a feedback loop, so disable them.
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto* wip = dm.getWaveInDevice (i))
                wip->setStereoPair (! wip->getName().containsIgnoreCase ("monitor"));

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        {
            if (auto* wip = dm.getWaveInDevice (i))
            {
                const bool isMonitorPort = wip->getName().containsIgnoreCase ("monitor");
                wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                wip->setEnabled (! isMonitorPort);
            }
        }

        edit->ensureNumberOfAudioTracks (1);
        edit->getTransport().ensureContextAllocated();

        // Arm the stereo input onto track 1 by default.
        armTrack (0, true);

        if (auto p = edit->getMasterPluginList().insertPlugin (te::LevelMeterPlugin::create(), -1))
            if (auto* lm = dynamic_cast<te::LevelMeterPlugin*> (p.get()))
                lm->measurer.addClient (meterClient);

        edit->restartPlayback();
    }

    te::InputDeviceInstance* getWaveInputInstance()
    {
        for (auto instance : edit->getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice
                 && instance->getInputDevice().isEnabled())
                return instance;

        return nullptr;
    }

    void armTrack (int trackIndex, bool on)
    {
        auto tracks = te::getAudioTracks (*edit);

        if (auto* t = tracks[trackIndex])
        {
            if (auto* instance = getWaveInputInstance())
            {
                if (on)
                    [[maybe_unused]] auto r = instance->setTarget (t->itemID, true, &edit->getUndoManager(), 0);

                instance->setRecordingEnabled (t->itemID, on);
            }
        }
    }

    bool isTrackArmed (te::AudioTrack& t)
    {
        for (auto instance : edit->getAllInputDevices())
            if (te::isOnTargetTrack (*instance, t, 0) && instance->isRecordingEnabled (t.itemID))
                return true;

        return false;
    }

    void handleCommand (const juce::var& v)
    {
        const auto cmd = v.getProperty ("cmd", {}).toString();
        auto& transport = edit->getTransport();

        const auto trackIndex = (int) v.getProperty ("track", -1);
        const auto getTrack = [&]() -> te::AudioTrack*
        {
            auto tracks = te::getAudioTracks (*edit);
            return juce::isPositiveAndBelow (trackIndex, tracks.size()) ? tracks[trackIndex] : nullptr;
        };

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
                sendTracks();
            }
        }
        else if (cmd == "record")
        {
            if (transport.isRecording())
            {
                transport.stop (false, false);
                te::EditFileOperations (*edit).save (true, true, false);
                sendTracks();
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
        else if (cmd == "seek")
        {
            transport.setPosition (te::TimePosition::fromSeconds ((double) v.getProperty ("seconds", 0.0)));
        }
        else if (cmd == "addTrack")
        {
            edit->ensureNumberOfAudioTracks (te::getAudioTracks (*edit).size() + 1);
            te::EditFileOperations (*edit).save (true, true, false);
            sendTracks();
        }
        else if (cmd == "removeTrack")
        {
            if (auto* t = getTrack())
            {
                edit->deleteTrack (t);
                te::EditFileOperations (*edit).save (true, true, false);
                sendTracks();
            }
        }
        else if (cmd == "arm")
        {
            armTrack (trackIndex, (bool) v.getProperty ("on", false));
            sendTracks();
        }
        else if (cmd == "mute")
        {
            if (auto* t = getTrack())
                t->setMute ((bool) v.getProperty ("on", false));
            sendTracks();
        }
        else if (cmd == "solo")
        {
            if (auto* t = getTrack())
                t->setSolo ((bool) v.getProperty ("on", false));
            sendTracks();
        }
        else if (cmd == "volume")
        {
            if (auto* t = getTrack())
                if (auto* vp = t->getVolumePlugin())
                    vp->setVolumeDb (juce::jlimit (-100.0f, 6.0f, (float) (double) v.getProperty ("db", 0.0)));
            sendTracks();
        }
        else if (cmd == "pan")
        {
            if (auto* t = getTrack())
                if (auto* vp = t->getVolumePlugin())
                    vp->setPan (juce::jlimit (-1.0f, 1.0f, (float) (double) v.getProperty ("value", 0.0)));
            sendTracks();
        }
        else if (cmd == "peaks")
        {
            sendPeaks (trackIndex, (int) v.getProperty ("clip", -1));
        }
        else if (cmd == "export")
        {
            doExport (v.getProperty ("format", "wav").toString(),
                      (int) v.getProperty ("bitDepth", 24));
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
        // App latency: one period at our (pinned) buffer size. System latency:
        // whatever the device layer reports downstream of us.
        const auto periodMs = 1000.0 * dev->getCurrentBufferSizeSamples() / dev->getCurrentSampleRate();
        auto systemMs = dm.getOutputLatencySeconds() * 1000.0;
        if (systemMs <= 0.0)
            systemMs = periodMs;
        o->setProperty ("latencyMs", periodMs);
        o->setProperty ("systemLatencyMs", systemMs);
        server.broadcast (juce::JSON::toString (juce::var (o), true) + "\n");

        sendTracks();
    }

    void sendTracks()
    {
        juce::Array<juce::var> tracks;

        for (auto t : te::getAudioTracks (*edit))
        {
            juce::Array<juce::var> clips;

            for (auto* c : t->getClips())
            {
                auto* co = new juce::DynamicObject();
                co->setProperty ("id", (juce::int64) c->itemID.getRawID());
                co->setProperty ("name", c->getName());
                co->setProperty ("start", c->getPosition().getStart().inSeconds());
                co->setProperty ("length", c->getPosition().getLength().inSeconds());

                if (auto* wc = dynamic_cast<te::WaveAudioClip*> (c))
                {
                    co->setProperty ("takes", std::max (1, wc->getTakes().size()));
                    co->setProperty ("currentTake", wc->getCurrentTake());
                }

                clips.add (juce::var (co));
            }

            auto* to = new juce::DynamicObject();
            to->setProperty ("name", t->getName());
            to->setProperty ("armed", isTrackArmed (*t));
            to->setProperty ("mute", t->isMuted (false));
            to->setProperty ("solo", t->isSolo (false));

            if (auto* vp = t->getVolumePlugin())
            {
                to->setProperty ("volumeDb", vp->getVolumeDb());
                to->setProperty ("pan", vp->getPan());
            }

            to->setProperty ("clips", clips);
            tracks.add (juce::var (to));
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("event", "tracks");
        o->setProperty ("editLength", edit->getLength().inSeconds());
        o->setProperty ("tracks", tracks);
        server.broadcast (juce::JSON::toString (juce::var (o), true) + "\n");
    }

    void sendPeaks (int trackIndex, int clipIndex)
    {
        auto tracks = te::getAudioTracks (*edit);
        if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
            return;

        auto clips = tracks[trackIndex]->getClips();
        if (! juce::isPositiveAndBelow (clipIndex, clips.size()))
            return;

        auto* ac = dynamic_cast<te::AudioClipBase*> (clips[clipIndex]);
        if (ac == nullptr)
            return;

        auto file = ac->getSourceFileReference().getFile();

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return;

        constexpr int peaksPerSecond = 50;
        const auto numBuckets = juce::jlimit (1, 12000,
            (int) (reader->lengthInSamples / reader->sampleRate * peaksPerSecond));
        const auto bucketSize = reader->lengthInSamples / numBuckets;

        juce::Array<juce::var> data;
        data.ensureStorageAllocated (numBuckets);

        for (int b = 0; b < numBuckets; ++b)
        {
            juce::Range<float> levels[2];
            reader->readMaxLevels (b * bucketSize, bucketSize, levels,
                                   juce::jmin (2, (int) reader->numChannels));

            float mag = 0;
            for (int ch = 0; ch < juce::jmin (2, (int) reader->numChannels); ++ch)
                mag = juce::jmax (mag, std::abs (levels[ch].getStart()), std::abs (levels[ch].getEnd()));

            data.add (std::round (mag * 1000.0f) / 1000.0f);
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("event", "peaks");
        o->setProperty ("track", trackIndex);
        o->setProperty ("clip", clipIndex);
        o->setProperty ("id", (juce::int64) clips[clipIndex]->itemID.getRawID());
        o->setProperty ("peaksPerSecond", peaksPerSecond);
        o->setProperty ("data", data);
        server.broadcast (juce::JSON::toString (juce::var (o), true) + "\n");
    }

    void doExport (const juce::String& format, int bitDepth)
    {
        const bool flac = format.equalsIgnoreCase ("flac");

        auto exportDir = juce::File::getCurrentWorkingDirectory().getChildFile ("exports");
        exportDir.createDirectory();
        auto outFile = exportDir.getNonexistentChildFile (
            "EspressoStudio_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S"),
            flac ? ".flac" : ".wav");

        te::Renderer::Parameters params (*edit);
        params.tracksToDo = te::toBitSet (te::getAllTracks (*edit));
        params.destFile = outFile;
        params.audioFormat = flac ? (juce::AudioFormat*) &flacFormat : (juce::AudioFormat*) &wavFormat;
        params.bitDepth = flac ? juce::jmin (bitDepth, 24) : bitDepth;
        params.sampleRateForAudio = engine.getDeviceManager().getSampleRate();
        params.blockSizeForAudio = 512;
        params.time = { te::TimePosition(), te::toPosition (edit->getLength()) };

        auto result = te::Renderer::renderToFile ("Export", params);

        auto* o = new juce::DynamicObject();
        o->setProperty ("event", "exported");
        o->setProperty ("ok", result.existsAsFile() && result.getSize() > 0);
        o->setProperty ("file", result.getFullPathName());
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
                        std::make_unique<EspressoUIBehaviour>(),
                        std::make_unique<EspressoEngineBehaviour>() };
    std::unique_ptr<te::Edit> edit;
    te::LevelMeasurer::Client meterClient;
    CallbackCounter callbackCounter;
    juce::WavAudioFormat wavFormat;
    juce::FlacAudioFormat flacFormat;
    ControlServer server;
};

//==============================================================================
int main()
{
    // Ask PipeWire for a pro-audio quantum (overridable from the environment),
    // and pin it: force-quantum holds the graph at our buffer size while the
    // engine runs, so other desktop apps can't raise our latency mid-session.
    setenv ("PIPEWIRE_LATENCY", "128/48000", /*overwrite*/ 0);
    setenv ("PIPEWIRE_PROPS", "{ node.force-quantum = 128 }", /*overwrite*/ 0);

    juce::ScopedJuceInitialiser_GUI juceInit;

    EspressoApp app;

    if (! app.initialise())
        return 1;

    juce::MessageManager::getInstance()->runDispatchLoop();
    return 0;
}
