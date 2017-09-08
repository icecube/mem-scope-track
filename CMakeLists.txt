cmake_minimum_required(VERSION 3.9)

include_directories(include)

add_library(memscopetrack SHARED
    src/preload.cxx
    src/track.cxx
)
target_link_libraries(memscopetrack dl)
target_compile_features(memscopetrack
    PRIVATE
        cxx_std_14
)

add_library(testing SHARED
    src/test.cxx
)
target_compile_features(testing
    PRIVATE
        cxx_std_14
)

enable_testing()

if (CMAKE_CONFIGURATION_TYPES)
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} 
        --force-new-ctest-process --output-on-failure 
        --build-config "$<CONFIGURATION>")
else()
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} 
        --force-new-ctest-process --output-on-failure)
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
