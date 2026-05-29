# pwviz

`pwviz` is a small PipeWire audio visualizer for Linux desktops. It captures the
current audio stream through PipeWire, runs a real-time FFT over the samples, and
draws a GTK4 spectrum bar display.

The project is intentionally small and split into focused C modules for
PipeWire capture, the shared audio buffer, FFT analysis, spectrum binning, and
GTK visualization/configuration. Runtime dependencies are PipeWire, GTK4,
gtk4-layer-shell, FFTW, and libspa.

The window is designed for Wayland compositors that support the wlr-layer-shell
protocol. It runs as a semitransparent overlay layer surface, stays above normal
windows, and is fully click-through.

## Requirements

- PipeWire
- GTK4 development headers
- gtk4-layer-shell development headers
- FFTW single-precision development headers (`fftw3f`)
- libspa development headers
- Meson and Ninja
- A C compiler

Package names differ by distribution, but on Arch-based systems the main
packages are typically `pipewire`, `gtk4`, `gtk4-layer-shell`, `fftw`, `meson`,
and `ninja`.

## Build

```sh
meson setup build
meson compile -C build
```

## Run

```sh
./build/pwviz
```

If there is no visible activity, make sure PipeWire is running and that an audio
stream is currently playing.

## Configuration

The overlay window background and border are fully transparent; only the
spectrum bars and peaks render with alpha. Use `Ctrl+Shift+Alt+F12` to open the
settings window.
Settings include analyzer mode, bar count, block size, peak behavior, alpha,
colours, and built-in colour profiles.

Settings are saved to:

```text
~/.config/pwviz/config.ini
```

## Disclaimer

This project was created fully using vibe coding with Codex. Treat the code as an
experimental personal project, not production-grade audio software.
