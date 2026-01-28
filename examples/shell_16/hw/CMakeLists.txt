######################################################################################
# POS Shell with 16 vFPGAs - Resource Usage Evaluation Build
#
# This CMake configuration creates a POS shell with:
# - 16 vFPGA regions
# - vIO Switch (16-port) for inter-vFPGA routing
# - vFIU (vFPGA Isolation Unit) for each vFPGA
# - Both TCP and RDMA network stacks enabled
# - VIU (Virtual Isolation Unit) for network isolation
#
# MIT Licence
# Copyright (c) 2025, Systems Group, ETH Zurich
######################################################################################

# CMake configuration
cmake_minimum_required(VERSION 3.5)
set(CYT_DIR ${CMAKE_SOURCE_DIR}/../../../)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CYT_DIR}/cmake)
find_package(CoyoteHW REQUIRED)

project(pos_shell_16vfpga)
message("*** POS Shell: 16 vFPGAs with vIO Switch, vFIU, TCP & RDMA ***")

#
# Network Configuration - Both stacks enabled
#
set(EN_RDMA 1)          # Enable RDMA stack
set(EN_TCP 1)           # Enable TCP stack

#
# vFPGA Configuration - 16 regions
#
set(N_REGIONS 16)       # 16 vFPGA regions

#
# Stream Configuration
#
set(EN_STRM 1)          # Enable host streams
set(N_STRM_AXI 2)       # 2 host stream interfaces per vFPGA

#
# Memory Configuration (HBM for u55c/u280)
#
set(EN_MEM 1)           # Enable memory interface
set(N_CARD_AXI 1)       # 1 memory interface per vFPGA

#
# Build Optimization - Required for timing closure with both stacks
#
set(BUILD_OPT 1)

#
# Target Device - u55c (HBM, good for multi-region)
#
set(FDEV_NAME "u55c")

#
# Compilation cores (adjust based on your machine)
#
set(COMP_CORES 32)

# Confirm that the selected options are allowed
validation_checks_hw()

#
# Load a simple passthrough application for all 16 vFPGAs
# Using the same simple app for all regions for resource evaluation
#
load_apps (
    VFPGA_C0_0 "src"
    VFPGA_C0_1 "src"
    VFPGA_C0_2 "src"
    VFPGA_C0_3 "src"
    VFPGA_C0_4 "src"
    VFPGA_C0_5 "src"
    VFPGA_C0_6 "src"
    VFPGA_C0_7 "src"
    VFPGA_C0_8 "src"
    VFPGA_C0_9 "src"
    VFPGA_C0_10 "src"
    VFPGA_C0_11 "src"
    VFPGA_C0_12 "src"
    VFPGA_C0_13 "src"
    VFPGA_C0_14 "src"
    VFPGA_C0_15 "src"
)

# Create the hardware project
create_hw()
