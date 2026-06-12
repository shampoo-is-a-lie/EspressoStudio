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
toolbox create espresso-dev
toolbox run -c espresso-dev sudo dnf install -y cmake gcc-c++ git \
    alsa-lib-devel pipewire-jack-audio-connection-kit-devel ladspa-devel \
    libcurl-devel freetype-devel fontconfig-devel libX11-devel \
    libXcomposite-devel libXcursor-devel libXext-devel libXinerama-devel \
    libXrandr-devel libXrender-devel mesa-libGL-devel mesa-libGLU-devel

# fetch Tracktion Engine (JUCE submodule uses an SSH URL upstream, hence the rewrite)
git -c url."https://github.com/".insteadOf="git@github.com:" \
    clone --depth 1 --recurse-submodules --shallow-submodules \
    https://github.com/Tracktion/tracktion_engine.git engine/libs/tracktion_engine

# build
toolbox run -c espresso-dev bash -c \
    'cd engine && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)'
```

### Run the app (on the host)

```bash
cd app && npm install && npm run dev
```

The Electron app spawns the engine via `toolbox run` automatically (override
with `ESPRESSO_ENGINE_CMD=/path/to/EspressoEngine`).

## Roadmap

1. ~~Phase 0 — spike: engine ↔ UI loop, record/play/meters~~ *(you are here)*
2. Phase 1 — tracks, takes & comping, mixer, save/load, WAV/FLAC export
3. Phase 2 — region editing, fades, automation
4. Phase 3 — VST3 / LV2 / CLAP hosting
5. Phase 4 — yabridge integration: Windows plugins, Melodyne
6. Phase 5 — the crema: Logic-grade polish, AppImage

## License

GPL-3.0 — Tracktion Engine is used under its GPLv3 licence.
