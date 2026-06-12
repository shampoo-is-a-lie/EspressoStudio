# EspressoStudio

Linux DAW. Two processes: `engine/` (C++, Tracktion Engine + JUCE, headless,
real-time audio) and `app/` (Electron + Svelte 4 + electron-vite UI). They talk
newline-delimited JSON over TCP `127.0.0.1:7177` (protocol documented at the
top of `engine/src/Main.cpp`).

## Build & run

- Host is Bazzite (Fedora ostree) — **the engine must be built and run inside
  the `espresso-dev` toolbox container**; deps are listed in README.md.
  - The container's Fedora release MUST match the host's PipeWire generation
    (`toolbox create -y -r 44`). A mismatched pipewire-jack client (F42 libs vs
    F44 server) spins millions of empty process callbacks/sec: the transport
    freezes at position 0 and rtkit's ~200ms RLIMIT_RTTIME budget is overrun,
    which is a *silent kernel SIGKILL* ~2s after startup. Cost us hours.
  - Build: `toolbox run -c espresso-dev bash -c 'cd engine && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)'`
  - Binary lands at `engine/build/EspressoEngine` (post-build copy).
- Start/stop the engine ONLY via `scripts/engine-ctl.sh {start|stop|status|log}`.
  It daemonizes inside the container (setsid+nohup, spawning session exits
  immediately) — anything else gets reaped by podman exec teardown.
- The app runs on the host: `cd app && npm run dev`. Its main process
  auto-spawns the engine through `engine-ctl.sh` (see `app/src/main/engineLink.js`).
- `engine/libs/tracktion_engine` is a plain shallow clone, gitignored (not a
  submodule — upstream's JUCE submodule URL is SSH-only, needs the
  `url.insteadOf` rewrite from README.md).
- Local patches needed after a fresh Tracktion clone (GCC 16 / new libstdc++):
  - `modules/3rd_party/nanorange/nanorange.hpp` ~line 17108: `y.value` →
    `y.value_` (uninstantiated-template-body error, `-Wtemplate-body`).
  - `modules/3rd_party/choc/audio/choc_SampleBuffers.h`: add
    `#include <cstdint>` to the include block.

## Conventions

- Audio-engine rule: everything real-time lives in the engine process; the UI
  is a control surface only. Don't add audio processing to Electron.
- When touching Tracktion APIs, check the pinned examples first
  (`engine/libs/tracktion_engine/examples/DemoRunner/demos/`,
  `examples/common/Utilities.h`) — the API moves; the pinned commit is truth.
- JUCE code style in `engine/` follows JUCE conventions (spaces before parens);
  app code follows the OAKANIZER style (2-space, no semicolons omitted).
- Plugin scanning is in-process (a crashing plugin kills the engine —
  out-of-process scanning is future work). JUCE's default Linux search paths
  miss Fedora's `/usr/lib64/{vst3,lv2}`; the engine adds them explicitly.
  Test plugins live in the container: `lsp-plugins-vst3`, `lv2-swh-plugins`.
  Plugin editor windows are engine-process native windows (never embedded in
  Electron); the known-plugin list persists via Tracktion's PropertyStorage
  (`~/.config/EspressoStudio/`).
- Session/recording scratch data goes in `session/`, renders in `exports/`
  (both gitignored). `spike-session/` is the retired Phase 0 scratch dir.
- The renderer state layer is `app/src/renderer/src/stores.js` — components
  never talk to the socket directly; they read stores and call `send()`.
  The engine is authoritative: commands don't update UI state optimistically,
  the resulting `tracks` event does.
