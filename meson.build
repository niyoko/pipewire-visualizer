project('pipewire-visualizer', 'c', default_options: ['warning_level=2'])

cc = meson.get_compiler('c')
if get_option('native_cpu')
  add_project_arguments(
    cc.get_supported_arguments(['-march=native', '-mtune=native']),
    language: 'c',
  )
endif

m_dep = cc.find_library('m', required: true)

deps = [
  dependency('libpipewire-0.3'),
  dependency('gtk4'),
  dependency('gtk4-layer-shell-0'),
  dependency('fftw3f'),
  dependency('json-glib-1.0'),
  dependency('libspa-0.2'),
  dependency('libsoup-3.0'),
  m_dep,
]

sources = [
  'src/audio_buffer.c',
  'src/app_config.c',
  'src/binning.c',
  'src/fft.c',
  'src/global_shortcut.c',
  'src/lyrics.c',
  'src/main.c',
  'src/now_playing.c',
  'src/pipewire_sink.c',
  'src/visualization.c',
]

executable('pipewire-visualizer', sources, dependencies: deps, install: true)

install_data(
  'data/pipewire-visualizer.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'data/pipewire-visualizer-autostart.desktop',
  rename: 'pipewire-visualizer.desktop',
  install_dir: get_option('sysconfdir') / 'xdg' / 'autostart',
)

install_data(
  'README.md',
  install_dir: get_option('datadir') / 'doc' / 'pipewire-visualizer',
)

install_data(
  'README.md',
  install_dir: get_option('datadir') / 'licenses' / 'pipewire-visualizer',
)
