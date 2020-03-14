project('scriptbots', 'cpp')

conf_data = configuration_data()
conf_data.set('HAVE_OPENMP', get_option('openmp'))
conf_data.set('OPENGL', get_option('opengl'))
configure_file(input : 'config.h.in',
               output : 'config.h',
               configuration : conf_data)

configuration_inc = [
  include_directories('.'),
  include_directories('include')
]

cpp = meson.get_compiler('cpp')
cppargs = []

incdir = include_directories('include')
boost_dep = dependency('boost', modules : ['serialization'])

deps = [
  boost_dep
]

srcfiles = [
  'src/Agent.cpp',
  'src/Base.cpp',
  'src/MLPBrain.cpp',
  'src/PerfTimer.cpp',
  'src/View.cpp',
  'src/World.cpp',
  'src/main.cpp',
  'src/vmath.cpp'
]

if get_option('opengl')
  deps += cpp.find_library('glut')
  deps += cpp.find_library('GL')
  srcfiles += 'src/GLView.cpp'
endif
if get_option('openmp')
  deps += cpp.find_library('gomp')
  cppargs += '-fopenmp'
endif

executable('scriptbots', srcfiles, include_directories : configuration_inc,
cpp_args: cppargs, dependencies : deps)
