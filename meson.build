project('scriptbots', 'c')

conf_data = configuration_data()
conf_data.set('OPENGL', get_option('opengl'))
configure_file(input : 'config.h.in',
               output : 'config.h',
               configuration : conf_data)

configuration_inc = [
  include_directories('.'),
  include_directories('include')
]

c = meson.get_compiler('c')
cargs = ['-pthread']

add_global_link_arguments(['-lm'], language: 'c')

incdir = include_directories('include')

deps = [
]

srcfiles = [
  'src/Agent.c',
  'src/vec.c',
  'src/helpers.c',
  'src/Base.c',
  'src/MLPBrain.c',
  'src/World.c',
  'src/main.c',
  'src/vec2f.c',
  'src/queue.c'
]

if get_option('opengl')
  deps += c.find_library('glut')
  deps += c.find_library('GL')
  srcfiles += 'src/GLView.c'
endif

executable('scriptbots', srcfiles, include_directories : configuration_inc,
c_args: cargs, dependencies : deps)

test_vec2f = executable('test_vec2f', ['tests/vec2f.c', 'src/vec2f.c'],
include_directories : configuration_inc)

test('vec2f tests', test_vec2f)
