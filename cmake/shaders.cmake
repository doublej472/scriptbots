function(add_shaders)
  set(SHADER_SOURCE_FILES ${ARGN}) # the rest of arguments to this function will be assigned as shader source files

  # Validate that source files have been passed
  list(LENGTH SHADER_SOURCE_FILES FILE_COUNT)
  if(FILE_COUNT EQUAL 0)
    message(FATAL_ERROR "Cannot create a shaders target without any source files")
  endif()

  set(SHADER_COMMANDS)
  set(SHADER_PRODUCTS)

  foreach(SHADER_SOURCE IN LISTS SHADER_SOURCE_FILES)
    cmake_path(ABSOLUTE_PATH SHADER_SOURCE NORMALIZE)
    cmake_path(GET SHADER_SOURCE FILENAME SHADER_NAME)
    
    # Build command
    list(APPEND SHADER_COMMANDS COMMAND)
    list(APPEND SHADER_COMMANDS Vulkan::glslc)
    list(APPEND SHADER_COMMANDS "${SHADER_SOURCE}")
    list(APPEND SHADER_COMMANDS "-o")
    list(APPEND SHADER_COMMANDS "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    # Add product
    list(APPEND SHADER_PRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

  endforeach()

  message(STATUS "cmd: ${SHADER_COMMANDS}")

  add_custom_target(shaders ALL
    ${SHADER_COMMANDS}
    COMMENT "Compiling Shaders"
    SOURCES ${SHADER_SOURCE_FILES}
    BYPRODUCTS ${SHADER_PRODUCTS}
  )

endfunction()