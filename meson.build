project('pwviz', 'c', default_options: ['warning_level=2'])

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

executable('pwviz', 'main.c', dependencies: deps)
