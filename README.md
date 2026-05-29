# pwviz

`pwviz` is a small PipeWire audio visualizer for Linux desktops. It captures the
current audio stream through PipeWire, runs a real-time FFT over the samples, and
draws a GTK4 spectrum bar display.

The project is intentionally minimal: one C source file, a Meson build file, and
runtime dependencies on PipeWire, GTK4, gtk4-layer-shell, FFTW, and libspa.

The window is designed for Wayland compositors that support the wlr-layer-shell
protocol. It runs as a semitransparent overlay layer surface, stays above normal
windows, and is click-through except for its top drag handle and bottom-right
resize grip.

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

## Disclaimer

This project was created fully using vibe coding with Codex. Treat the code as an
experimental personal project, not production-grade audio software.
