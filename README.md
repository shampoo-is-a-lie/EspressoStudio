<div align="center">

# ☕ EspressoStudio

### *a DAW for Linux, pulled like a perfect shot*

![status](https://img.shields.io/badge/status-first%20extraction-ffb000?style=flat-square)
![platform](https://img.shields.io/badge/platform-Linux-1d2420?style=flat-square)
![engine](https://img.shields.io/badge/engine-Tracktion%20%2B%20JUCE-9EAAB8?style=flat-square)
![ui](https://img.shields.io/badge/UI-Electron%20%2B%20Svelte-D4A373?style=flat-square)
![license](https://img.shields.io/badge/license-GPL--3.0-D4A373?style=flat-square)

</div>

---

```console
espressostudio@linux:~$ ./pull-shot
[ ok ] grinding audio engine........... EspressoEngine (C++ / Tracktion)
[ ok ] heating boiler.................. PipeWire @ pro-audio quantum
[ ok ] steaming interface.............. Electron + Svelte
[ ok ] first drops..................... transport · record · meters

▓▓ ESPRESSO STUDIO ▓▓
> phase 0 — the spike shot. brewing…
```

---

## What this wants to be

A beautiful, Logic-Pro-inspired DAW for Linux: multitrack/multitake recording,
ultra-low-latency monitoring, surgical editing and mixing, high-resolution
export, native VST3/LV2/CLAP hosting — and Windows-only plugins (looking at
you, Melodyne) through yabridge + Wine, old-school transfer style.

## How it's built — two processes, one drink

| Process | What | Why |
|---------|------|-----|
| **EspressoEngine** | headless C++ process — Tracktion Engine + JUCE | the real-time half: audio I/O, recording, playback, plugins. A UI hiccup can never glitch audio. |
| **EspressoStudio** | Electron + Svelte app | the beautiful half: arrange view, mixer, transport. Talks to the engine over newline-delimited JSON on `127.0.0.1:7177`. |

## Phase 0 — the spike (current)

Prove the foundation: engine opens the audio device through PipeWire's JACK
interface, records from your inputs onto a track, plays back, and streams
meters to a Svelte transport UI at 30 Hz.

### Build the engine (in a toolbox/distrobox container)

```bash
# once: create the container and install deps
# (-r <release>: MUST match your host's PipeWire generation — a mismatched
#  pipewire-jack client spins empty callbacks and gets silently SIGKILLed)
toolbox create -y -r 44 espresso-dev
toolbox run -c espresso-dev sudo dnf install -y cmake gcc-c++ git libatomic \
    alsa-lib-devel pipewire-jack-audio-connection-kit-devel ladspa-devel \
    libcurl-devel freetype-devel fontconfig-devel libX11-devel \
    libXcomposite-devel libXcursor-devel libXext-devel libXinerama-devel \
    libXrandr-devel libXrender-devel mesa-libGL-devel mesa-libGLU-devel

# fetch Tracktion Engine (JUCE submodule uses an SSH URL upstream, hence the rewrite)
git -c url."https://github.com/".insteadOf="git@github.com:" \
    clone --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/Tracktion/tracktion_engine.git engine/libs/tracktion_engine

# two one-character patches for GCC 16 / new libstdc++ (see CLAUDE.md):
#   nanorange.hpp ~17108:  y.value  →  y.value_
#   choc_SampleBuffers.h:  add  #include <cstdint>

# build
toolbox run -c espresso-dev bash -c \
    'cd engine && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)'

# run it (daemonizes inside the container)
bash scripts/engine-ctl.sh start   # also: stop / status / log
```

### Run the app (on the host)

```bash
cd app && npm install && npm run dev
```

The Electron app spawns the engine via `toolbox run` automatically (override
with `ESPRESSO_ENGINE_CMD=/path/to/EspressoEngine`).

## Phase 0 results (2026-06-12)

Verified end-to-end on Bazzite (Fedora 44, PipeWire 1.6.4):

- JACK-via-PipeWire device at **48 kHz / 128 samples / 2.0 ms output latency**,
  audio threads at realtime priority (rtkit)
- record → per-track takes (`Track 1_Take_1.wav`…), stop, playback with live
  meters streaming to the UI at 30 Hz
- Electron UI links to the engine, shows device/SR/buffer/latency, drives
  transport

## Phase 1 results (2026-06-12)

The arrange era begins: track headers with **record-arm / mute / solo /
volume / pan**, add & remove tracks, a canvas **arrange view** with ruler,
grid, waveform clips, take badges, click-to-seek and zoom, and **WAV/FLAC
export** (24-bit) from the transport bar. The engine speaks protocol v1
(documented at the top of `engine/src/Main.cpp`) and serves multiple
control clients at once.

## Phase 2 results (2026-06-12)

The editing era: **drag clips** to move them (snap to grid, Alt to bypass),
**trim** either edge by the clip border, **split** at the playhead (✂ /
Ctrl+T), **delete**, **fade in/out** via draggable handles with ramp
rendering, **loop ranges** drawn on the ruler (drag to set), **loop
recording stacks takes** with a click-to-cycle take badge (comping v1), and
full **undo/redo** (↶↷ / Ctrl+Z). Keyboard: Space = play/stop, R = record.

## Phase 3 results (2026-06-12)

The plugin era: the engine scans **VST3 and LV2** plugins (including
Fedora's `/usr/lib64` dirs, which JUCE's defaults miss) and hosts them on
track chains. Each track header has an **fx** button opening a chain panel:
searchable plugin browser, add/remove, bypass, and a **ui** button that
opens the plugin's own native editor as a floating window owned by the
engine process — verified with LSP (VST3) and SWH (LV2). CLAP waits on
upstream Tracktion hosting support; Windows VSTs via yabridge are Phase 4.

## Phase 4 results (2026-06-12)

The bridge: **Windows VST3 plugins run inside EspressoStudio** via Wine +
[yabridge](https://github.com/robbert-vdh/yabridge). `scripts/setup-bridge.sh`
installs Wine and yabridge into the engine container and wires the standard
Wine VST3 directory; bridged plugins then appear in the plugin browser like
any other VST3 — verified with Dexed's Windows build, including its full
editor GUI rendered through Wine.

### Melodyne

Install Melodyne's Windows version into the Wine prefix
(`toolbox run -c espresso-dev wine 'Melodyne 5 Installer.exe'`), run
`scripts/setup-bridge.sh` again, rescan, and insert Melodyne on a track.
**Workflow note:** ARA does not cross the Wine bridge, so Melodyne runs the
classic way — insert it, play the section through once to *transfer* the
audio into Melodyne, then edit. Exactly how it worked for its first decade.

## Roadmap

1. ~~Phase 0 — spike: engine ↔ UI loop, record/play/meters~~ *(done)*
2. ~~Phase 1 — tracks, mixer, arrange view, WAV/FLAC export~~ *(done)*
3. ~~Phase 2 — region editing, fades, loop + takes/comping, undo~~ *(done)*
4. ~~Phase 3 — VST3 / LV2 hosting, FX chains, native plugin editors~~ *(done — automation lanes + CLAP pending)*
5. ~~Phase 4 — yabridge bridge: Windows VST3s, Melodyne path~~ *(done)*
6. Phase 4½ — the guitar shelf: built-in **Neural Amp Modeler** (headless
   [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore),
   MIT) as a native engine plugin with its own in-app UI, plus an **impulse
   response loader** (cab sims via convolution) — no external plugin needed
   to track guitar
7. Phase 5 — the crema: Logic-grade polish, AppImage

## License

GPL-3.0 — Tracktion Engine is used under its GPLv3 licence.
