cmake_minimum_required(VERSION 3.22)

project(pep-cprep LANGUAGES C CXX VERSION 0.1.0)


include(CMakeDependentOption)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CPREP_MASTER_PROJECT ON)
else()
    set(CPREP_MASTER_PROJECT OFF)
endif()

cmake_dependent_option(CPREP_BUILD_TESTS "build tests of pep-cprep" ON "CPREP_MASTER_PROJECT" OFF)


file(GLOB_RECURSE CPREP_PUBLIC_SOURCES include/*.hpp)
file(GLOB_RECURSE CPREP_PRIVATE_SOURCES src/*.cpp src/*.hpp)
add_library(pep-cprep ${CPREP_PUBLIC_SOURCES} ${CPREP_PRIVATE_SOURCES})
target_compile_features(pep-cprep PUBLIC cxx_std_20)
target_include_directories(pep-cprep PUBLIC include)


if(CPREP_BUILD_TESTS)
    add_executable(cprep-test tests/test.cpp)
    target_link_libraries(cprep-test PRIVATE pep-cprep)
endif()
