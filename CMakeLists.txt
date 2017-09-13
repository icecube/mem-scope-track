cmake_minimum_required(VERSION 3.9)

find_package(Boost 1.51 REQUIRED COMPONENTS
             iostreams filesystem)

include_directories(include)

# the main LD_PRELOAD library
add_library(memscopetrack SHARED
    src/preload.cxx
    src/track.cxx
)
target_link_libraries(memscopetrack
    dl
    Boost::iostreams Boost::filesystem
)
target_compile_features(memscopetrack
    PRIVATE
        cxx_std_14
)

# python helpers
file(COPY ${CMAKE_SOURCE_DIR}/python DESTINATION ${CMAKE_BINARY_DIR})

# testing starts here
enable_testing()

# a library for testing
add_library(testing SHARED
    src/test.cxx
)
target_compile_features(testing
    PRIVATE
        cxx_std_14
)

# special "check" target to spew output on failure
if (CMAKE_CONFIGURATION_TYPES)
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} 
        --force-new-ctest-process --output-on-failure 
        --build-config "$<CONFIGURATION>"
        COMMAND rm -f mem-scope-track.*
    )
else()
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} 
        --force-new-ctest-process --output-on-failure
        COMMAND rm -f mem-scope-track.*
    )
endif()

configure_file(resources/tests/test_harness.py.in test_harness.py)

function(make_test NAME)
  add_executable(${NAME}
      resources/tests/${NAME}.cxx
  )
  target_link_libraries(${NAME} testing)
  set_target_properties(${NAME}
      PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY tests
  )
  target_compile_features(${NAME}
      PRIVATE
          cxx_std_14
  )
  add_test(${NAME} test_harness.py ${NAME})
  if(EXISTS ${CMAKE_SOURCE_DIR}/resources/tests/${NAME}.py)
    configure_file(resources/tests/${NAME}.py tests/${NAME}.py COPYONLY)
  endif()
endfunction(make_test)

make_test(test_01)
make_test(test_02)
make_test(test_03)
