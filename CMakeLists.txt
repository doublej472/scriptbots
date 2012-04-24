project(scriptbots)
cmake_minimum_required(VERSION 2.8)
find_package(Boost COMPONENTS serialization REQUIRED)
find_package(OpenGL)
find_package(GLUT)
find_package(OpenMP)

# Force to compile headless
#SET (GLUT_FOUND 0)

# Windows support (untested)
if (WIN32 AND NOT GLUT_FOUND)
   find_library(GLUT32_LIBRARY NAMES glut32 PATHS ${CMAKE_CURRENT_SOURCE_DIR})
  SET (LOCAL_GLUT32 1)
endif()

# Mac OS X support for XCode 4.2
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  SET (MAC_GLUT 1)
  #SET (CMAKE_CXX_COMPILER "/usr/bin/g++-4.2" )
  #SET (CMAKE_CC_COMPILER "/usr/bin/gcc" )
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I /usr/llvm-gcc-4.2/lib/gcc/i686-apple-darwin11/4.2.1/include")
  #SET (OPENMP_FOUND 1) # override
endif()

#GProf and general debugging
#SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -pg")

#Performance
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

# OpenMP Multithreading
if (OPENMP_FOUND)
  SET (HAVE_OPENMP 1)
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
  SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")

  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOPENMP=1")
else()
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOPENMP=1")
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${OPENGL_INCLUDE_DIRS} ${GLUT_INCLUDE_DIRS})

# Boost Serialization Library
include_directories(${Boost_INCLUDE_DIR})
link_directories(${Boost_LIBRARY_DIR})

# Compile required files
if (GLUT_FOUND)
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOPENGL=1")
  set( SB_SRCS
    View.cpp
    GLView.cpp
    main.cpp
    DWRAONBrain.cpp
    MLPBrain.cpp
    Agent.cpp
    Base.cpp
    PerfTimer.cpp
    World.cpp
    vmath.cpp )
else()
  SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOPENGL=0")
  set( SB_SRCS
    View.cpp
    main.cpp
    DWRAONBrain.cpp
    MLPBrain.cpp
    Agent.cpp
    Base.cpp
    PerfTimer.cpp
    World.cpp
    vmath.cpp )
endif()

add_executable(scriptbots  ${SB_SRCS})

if (WIN32 AND NOT GLUT_FOUND)
  target_link_libraries(scriptbots ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES} ${GLUT32_LIBRARY})
else()
  target_link_libraries(scriptbots ${OPENGL_LIBRARIES} ${GLUT_LIBRARIES})
endif()

# Boost:
target_link_libraries(scriptbots ${Boost_SERIALIZATION_LIBRARY})
target_link_libraries(scriptbots ${Boost_LIBRARIES})


# copy glut32.dll to the output dirs
# not sure how msvc could be true and win32 not, but just in case..
if (MSVC AND WIN32 AND NOT GLUT_FOUND) 
  file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/Debug)
  file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/Release)
  file(COPY glut32.dll DESTINATION ${PROJECT_BINARY_DIR}/Debug)
  file(COPY glut32.dll DESTINATION ${PROJECT_BINARY_DIR}/Release)
endif()

