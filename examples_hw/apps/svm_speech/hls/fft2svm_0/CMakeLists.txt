# CMakeLists.txt
cmake_minimum_required(VERSION 3.0)
set (PROJECT_NAME fft2svm_0)
project(${PROJECT_NAME})

if (NOT hasParent)
   if (DEVICE_NAME STREQUAL "u280")
      set(FPGA_PART xcu280-fsvh2892-2L-e)
      set(FPGA_FAMILY ultraplus)
   endif()
endif()

set(CLOCK_PERIOD 100 CACHE STRING "Target clock period in nanoseconds")

# Find Xilinx Vivado HLS
find_package(VivadoHLS REQUIRED)
if (NOT VIVADO_HLS_FOUND)
  message(FATAL_ERROR "Vivado HLS not found.")
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/make.tcl.in make.tcl)

set(EXAMPLE_HLS_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/fft2svm_0.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/fft2svm_0.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fft2svm_0_test.cpp)

#Setup HLS custom targets
set(HLS_TARGETS synthesis csim ip services)
foreach (target ${HLS_TARGETS})
   if (NOT TARGET ${target})
      add_custom_target(${target})
   endif()
   add_custom_target(${target}.${PROJECT_NAME}
      COMMAND ${VIVADO_HLS_BINARY} -f make.tcl -tclargs ${target}
      DEPENDS ${EXAMPLE_HLS_DEPENDS})
   add_dependencies(${target} ${target}.${PROJECT_NAME})
endforeach()