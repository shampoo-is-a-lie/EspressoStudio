# EspressoStudio

Linux DAW. Two processes: `engine/` (C++, Tracktion Engine + JUCE, headless,
real-time audio) and `app/` (Electron + Svelte 4 + electron-vite UI). They talk
newline-delimited JSON over TCP `127.0.0.1:7177` (protocol documented at the
top of `engine/src/Main.cpp`).

## Build & run

- Host is Bazzite (Fedora ostree) — **the engine must be built and run inside
  the `espresso-dev` toolbox container**; deps are listed in README.md.
  - Build: `toolbox run -c espresso-dev bash -c 'cd engine && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)'`
  - Binary lands at `engine/build/EspressoEngine` (post-build copy).
- The app runs on the host: `cd app && npm run dev`. Its main process
  auto-spawns the engine through `toolbox run` (see `app/src/main/engineLink.js`).
- `engine/libs/tracktion_engine` is a plain shallow clone, gitignored (not a
  submodule — upstream's JUCE submodule URL is SSH-only, needs the
  `url.insteadOf` rewrite from README.md).

## Conventions

- Audio-engine rule: everything real-time lives in the engine process; the UI
  is a control surface only. Don't add audio processing to Electron.
- When touching Tracktion APIs, check the pinned examples first
  (`engine/libs/tracktion_engine/examples/DemoRunner/demos/`,
  `examples/common/Utilities.h`) — the API moves; the pinned commit is truth.
- JUCE code style in `engine/` follows JUCE conventions (spaces before parens);
  app code follows the OAKANIZER style (2-space, no semicolons omitted).
- Session/recording scratch data goes in `spike-session/` (gitignored).
