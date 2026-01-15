cmake_minimum_required(VERSION 3.0)
project(test)

set(CYT_DIR ${CMAKE_SOURCE_DIR}/../)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CYT_DIR}/cmake)

find_package(CoyoteHW REQUIRED)

#
# TEST
#

#
# Static
# @brief: Default shell, contains interconnect and memory link testing circuits (perf_local)
#
# @note: Add a target device (FDEV_NAME)
#
# This is what every other shell is built against
#

if(EXAMPLE STREQUAL "static")
    set(BUILD_STATIC 1)
    set(BUILD_SHELL 0)
    set(BUILD_OPT 1)
    set(N_REGIONS 4)
    set(EN_STRM 1)
    set(EN_MEM 1)
    set(HBM_SPLIT 0)

    validation_checks_hw()

    load_apps (
        VFPGA_C0_0 "apps/perf_local"
        VFPGA_C0_1 "apps/perf_local"
        VFPGA_C0_2 "apps/perf_local"
        VFPGA_C0_3 "apps/perf_local"
    )

    create_hw()
endif()

