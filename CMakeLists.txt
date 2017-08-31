cmake_minimum_required(VERSION 3.9)

add_library(memscopetrack SHARED
    src/preload.cxx
)
target_link_libraries(memscopetrack dl)
target_compile_features(memscopetrack
    PRIVATE
        cxx_std_14
)