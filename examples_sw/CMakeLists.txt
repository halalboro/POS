cmake_minimum_required(VERSION 3.5)
project(test)

set(CYT_DIR ${CMAKE_SOURCE_DIR}/../)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CYT_DIR}/cmake)

find_package(CoyoteSW REQUIRED)

#
# Pass an additional command-line argument to specify the verbosity of the compilation 
# If VERBOSITY is passed as an argument, VERBOSE is set as preprocessor-macro
# 

set(VERBOSITY OFF CACHE BOOL "Enable VERBOSITY for compilation")

if (VERBOSITY)
    message("You selected VERBOSITY for compilation")
    add_compile_definitions(VERBOSE)
else()
    message("VERBOSITY is not selected for compilation")
endif()


#
# Shell reconfiguration
#
if(EXAMPLE STREQUAL "reconfigure_shell")
    set(TARGET_DIR "${CYT_DIR}/examples_sw/apps/reconfigure_shell")
    message("** Shell load")
endif()

#
# TEST
#
if(EXAMPLE STREQUAL "perf_gpu")
    set(TARGET_DIR "${CYT_DIR}/examples_sw/apps/perf_gpu")
    message("** Example: Validation tests - GPU perf")
endif()

#
# Example builds
#

# Used in conjunction with the default shell
if(EXAMPLE STREQUAL "perf_local")
    set(TARGET_DIR "${CYT_DIR}/examples_sw/apps/perf_local")
    message("** Example: Validation tests - Link perf")
endif()

# Used in conjunction with the default shell
if(EXAMPLE STREQUAL "perf_counter")
    set(TARGET_DIR "${CYT_DIR}/examples_sw/apps/perf_counter")
    message("** Example: Test perf couter")
endif()

#
# Create build targets
#
set(EXEC test)
add_executable(${EXEC} ${TARGET_DIR}/main.cpp)
target_link_libraries(${EXEC} PUBLIC Coyote)
target_link_directories(${EXEC} PUBLIC /usr/local/lib)

if(EN_GPU)
    set(AMD_GPU "gfx908" CACHE STRING "AMD GPU device to use")
    set_property(TARGET ${EXEC} PROPERTY HIP_ARCHITECTURES ${AMD_GPU})
    set_target_properties(${EXEC} PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(${EXEC} PUBLIC hip::device  numa pthread drm drm_amdgpu rt dl hsa-runtime64 hsakmt)
endif()
