######################################################################################
# P4Runtime Server Example
#
# Demonstrates running a P4Runtime gRPC server that allows remote clients
# to program FPGA routing tables.
#
# Usage:
#   ./p4runtime_server --vfid=0 --port=50051 [--debug=1]
#
# MIT License
# Copyright (c) 2025, Systems Group, ETH Zurich
######################################################################################

cmake_minimum_required(VERSION 3.15)

# Executable name
set(EXEC p4runtime_server)

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
target_link_libraries(${EXEC} PRIVATE
    p4runtime_server
    p4runtime_engine
    p4runtime_proto
    Coyote
    ${Boost_LIBRARIES}
)

# Set output directory
set_target_properties(${EXEC} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)

message(STATUS "  Building example: ${EXEC}")
