project('pipewire-visualizer', 'c', default_options: ['warning_level=2'])

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: true)

deps = [
  dependency('libpipewire-0.3'),
  dependency('gtk4'),
  dependency('gtk4-layer-shell-0'),
  dependency('fftw3f'),
  dependency('libspa-0.2'),
  m_dep,
]

sources = [
  'src/audio_buffer.c',
  'src/app_config.c',
  'src/binning.c',
  'src/fft.c',
  'src/global_shortcut.c',
  'src/main.c',
  'src/pipewire_sink.c',
  'src/visualization.c',
]

executable('pipewire-visualizer', sources, dependencies: deps, install: true)
