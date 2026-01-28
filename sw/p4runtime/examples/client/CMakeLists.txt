######################################################################################
# P4Runtime Client Example
#
# Demonstrates using the P4Runtime gRPC client to remotely program
# FPGA routing tables on a server.
#
# Usage:
#   ./p4runtime_client --server=192.168.1.100:50051 --action=add --ip=10.0.0.0/8 --mac=aa:bb:cc:dd:ee:ff --port=1
#   ./p4runtime_client --server=192.168.1.100:50051 --action=stats
#
# MIT License
# Copyright (c) 2025, Systems Group, ETH Zurich
######################################################################################

cmake_minimum_required(VERSION 3.15)

# Executable name
set(EXEC p4runtime_client)

# Source files
add_executable(${EXEC}
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

# Include directories
target_include_directories(${EXEC} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${PROTO_GEN_DIR}
)

# Link libraries
# Note: Client does NOT need Coyote library (no FPGA access needed)
target_link_libraries(${EXEC} PRIVATE
    p4runtime_client
    p4runtime_proto
    ${Boost_LIBRARIES}
)

# Set output directory
set_target_properties(${EXEC} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

message(STATUS "  Building example: ${EXEC}")
