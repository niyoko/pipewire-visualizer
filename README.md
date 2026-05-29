# pipewire-visualizer

`pipewire-visualizer` is a small PipeWire audio visualizer for Linux desktops.
It captures the current audio stream through PipeWire, runs a real-time FFT over
the samples, and draws a GTK4 spectrum bar display.

The project is intentionally small and split into focused C modules for
PipeWire capture, the shared audio buffer, FFT analysis, spectrum binning, and
GTK visualization/configuration. Runtime dependencies are PipeWire, GTK4,
gtk4-layer-shell, FFTW, and libspa.

The window is designed for Wayland compositors that support the wlr-layer-shell
protocol. It runs as a semitransparent overlay layer surface, stays above normal
windows, and is fully click-through.

## History

This project started from a long-time love for
[Winamp](https://winamp.com/) and its classic spectrum visualizer style. The
main visual reference is the Winamp-era
[Classic Spectrum Analyzer](https://winampheritage.com/visualization/classic-spectrum-analyzer/165966)
plug-in, with additional reference from the WACUP
[vis_classic](https://github.com/WACUP/vis_classic) source release.

The Linux app is not a direct port of the Winamp plug-in. PipeWire capture,
Wayland layer-shell overlay behavior, GTK4 drawing, configuration storage, and
packaging are specific to this project. The parts intentionally adapted from
the original Classic Spectrum Analyzer / WACUP `vis_classic` behavior are:

- FFT shaping controls: equalization, envelope power, and FFT scale.
- Spectrum binning: logarithmic bar-bin distribution with a 16 kHz high-end
  cutoff.
- Bar level calculation: selectable peak or average level per bar using
  0..255-style spectrum levels.
- Motion controls: bar falloff rate and peak change behavior.
- Classic visualizer setting categories: frequency bar style, background mode,
  peak colour behavior, and peak motion options.
- Visual geometry conventions: bar width, horizontal spacing, vertical block
  spacing, and profile-specific analyzer defaults.
- Built-in colour profile names and colour palettes inspired by the original
  Classic Spectrum Analyzer profiles.

For years I wanted a similar visualizer that works naturally on Linux with
system audio from YouTube, Spotify, and other music players instead of
being locked to one media player. I did not have the time to build it properly.
In 2026, with AI coding agents advanced enough to help move quickly through
desktop plumbing, PipeWire capture, FFT tuning, Wayland overlay behavior, and
packaging, it finally became possible to make this version.

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

## Install From AUR

On Arch Linux or an Arch-based distribution, install the release package from the
AUR:

```sh
paru -S pipewire-visualizer
```

or:

```sh
yay -S pipewire-visualizer
```

You can also build it manually:

```sh
git clone https://aur.archlinux.org/pipewire-visualizer.git
cd pipewire-visualizer
makepkg -si
```

## Build

```sh
meson setup build
meson compile -C build
```

## Run

```sh
./build/pipewire-visualizer
```

If there is no visible activity, make sure PipeWire is running and that an audio
stream is currently playing.

## Autostart

The packaged install includes an XDG autostart entry, so
`pipewire-visualizer` starts automatically when you log in to a desktop session.

For a local source build, install the desktop files manually:

```sh
mkdir -p ~/.local/share/applications ~/.config/autostart
cp data/pipewire-visualizer.desktop ~/.local/share/applications/
cp data/pipewire-visualizer-autostart.desktop \
  ~/.config/autostart/pipewire-visualizer.desktop
```

## Configuration

The overlay window background is transparent unless a background mode or alpha
is configured; spectrum bars and peaks render with alpha. Use
`Ctrl+Shift+Alt+F12` to open the settings window.
Settings include analyzer mode, bar count, WACUP-style FFT equalization,
envelope and scale, level mode, display threshold, falloff, window anchor,
margins, size, bar width, spacing, block size, peak behavior, alpha, bar
opacity, colours, Now Playing metadata, and built-in colour profiles.

The Style page exposes the Winamp-era categories shown by the
original visualizer: Frequency Bars (`Classic`, `Soft Flame`, `Fire`,
`Solid Lines`, `Winamp Fire`, `Random`), Background (`Black`, `Grid`,
`Solid Colour`, `Flash`, `Flash Grid`), Peak Colour (`Fade`, `Level`,
`Level & Fade`), and Motion (`Normal`, `Fall`, `Rise`, `Fall & Rise`,
`Rise Fall`, `Sparks`).

The Now Playing section uses MPRIS metadata from the session bus when a player
provides it. It can show the player app, track title, artist, and album in a
bottom strip, with configurable visibility, height, font size, and background
alpha.

Settings are saved to:

```text
~/.config/pipewire-visualizer/config.ini
```

## Disclaimer

This project was created fully using vibe coding with Codex. Treat the code as an
experimental personal project, not production-grade audio software.
