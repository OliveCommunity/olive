add_subdirectory(src)
add_subdirectory(c_api)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
