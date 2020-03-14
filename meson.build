project('scriptbots', 'cpp')

conf_data = configuration_data()
conf_data.set('HAVE_OPENMP', true)
conf_data.set('OPENGL', true)
configure_file(input : 'config.h.in',
               output : 'config.h',
               configuration : conf_data)

configuration_inc = [
  include_directories('.'),
  include_directories('include')
]

cpp = meson.get_compiler('cpp')

incdir = include_directories('include')
boost_dep = dependency('boost', modules : ['serialization'])

deps = [
  cpp.find_library('gomp'),
  cpp.find_library('glut'),
  cpp.find_library('GL'),
  boost_dep
]

srcfiles = [
  'src/Agent.cpp',
  'src/Base.cpp',
  'src/DWRAONBrain.cpp',
  'src/GLView.cpp',
  'src/MLPBrain.cpp',
  'src/PerfTimer.cpp',
  'src/View.cpp',
  'src/World.cpp',
  'src/main.cpp',
  'src/vmath.cpp'
]

executable('scriptbots', srcfiles, include_directories : configuration_inc,
cpp_args: '-fopenmp', dependencies : deps)
