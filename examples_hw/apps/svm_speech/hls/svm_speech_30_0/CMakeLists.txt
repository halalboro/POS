# CMakeLists.txt
cmake_minimum_required(VERSION 3.0)
set (PROJECT_NAME svm_speech_30_0)
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

set(EXAMPLE_HLS_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/MW_target_hardware_resources.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CompactClassificationECOC.c
    ${CMAKE_CURRENT_SOURCE_DIR}/CompactClassificationECOC.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rtGetInf.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rtGetInf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rtGetNaN.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rtGetNaN.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rt_nonfinite.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rt_nonfinite.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rtwtypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_0.c
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_0.h
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_data.h
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_initialize.c
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_initialize.h
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_internal_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_terminate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_terminate.h
    ${CMAKE_CURRENT_SOURCE_DIR}/svm_speech_30_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/main.h)

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