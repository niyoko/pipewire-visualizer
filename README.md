# pwviz

`pwviz` is a small PipeWire audio visualizer for Linux desktops. It captures the
current audio stream through PipeWire, runs a real-time FFT over the samples, and
draws a GTK4 spectrum bar display.

The project is intentionally minimal: one C source file, a Meson build file, and
runtime dependencies on PipeWire, GTK4, FFTW, and libspa.

## Requirements

- PipeWire
- GTK4 development headers
- FFTW single-precision development headers (`fftw3f`)
- libspa development headers
- Meson and Ninja
- A C compiler

Package names differ by distribution, but on Arch-based systems the main
packages are typically `pipewire`, `gtk4`, `fftw`, `meson`, and `ninja`.

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
