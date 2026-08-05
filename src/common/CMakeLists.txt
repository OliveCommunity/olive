add_subdirectory(src)
add_subdirectory(c_api)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
