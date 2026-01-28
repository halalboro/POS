######################################################################################
# P4Runtime Library for POS (Programmable OS)
#
# Provides P4-style control plane interface for FPGA routing table management.
#
# Components:
#   - POSRuntimeEngine: Core table management (translates P4 ops to CSR writes)
#   - P4RuntimeServer: gRPC server for remote table management
#   - P4RuntimeClient: gRPC client for remote connections
#
# Dependencies:
#   - POS/Coyote library (via FindCoyoteSW)
#   - gRPC and Protobuf
#   - Boost program_options
#
# MIT License
# Copyright (c) 2025, Systems Group, ETH Zurich
######################################################################################

cmake_minimum_required(VERSION 3.15)
project(P4Runtime VERSION 1.0.0 LANGUAGES CXX)

# =============================================================================
# Options
# =============================================================================
option(P4RUNTIME_BUILD_EXAMPLES "Build P4Runtime example applications" ON)
option(P4RUNTIME_BUILD_SERVER "Build P4Runtime server components" ON)
option(P4RUNTIME_BUILD_CLIENT "Build P4Runtime client components" ON)

# =============================================================================
# C++ Standard
# =============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -O3")

# =============================================================================
# Output Directories
# =============================================================================
if (NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
endif()

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
endif()

# =============================================================================
# Find Dependencies
# =============================================================================

# POS/Coyote library
# POS_DIR should be set by parent CMakeLists.txt or via command line
if(NOT DEFINED POS_DIR)
    set(POS_DIR ${CMAKE_SOURCE_DIR}/../..)
endif()
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${POS_DIR}/cmake)
find_package(CoyoteSW REQUIRED)

# Boost
find_package(Boost REQUIRED COMPONENTS program_options)

# gRPC and Protobuf
find_package(Protobuf REQUIRED)
find_package(gRPC QUIET)

# If gRPC not found via find_package, try pkg-config
if(NOT gRPC_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED grpc++ grpc)
    set(GRPC_LIBRARIES ${GRPC_LIBRARIES})
    set(GRPC_INCLUDE_DIRS ${GRPC_INCLUDE_DIRS})
else()
    set(GRPC_LIBRARIES gRPC::grpc++ gRPC::grpc)
endif()

# Find grpc_cpp_plugin
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin
    HINTS
        /usr/bin
        /usr/local/bin
        $ENV{GRPC_INSTALL_DIR}/bin
)
if(NOT GRPC_CPP_PLUGIN)
    message(FATAL_ERROR "grpc_cpp_plugin not found. Install gRPC or set GRPC_INSTALL_DIR.")
endif()

message(STATUS "P4Runtime Configuration:")
message(STATUS "  Protobuf version: ${Protobuf_VERSION}")
message(STATUS "  gRPC C++ plugin:  ${GRPC_CPP_PLUGIN}")
message(STATUS "  Build examples:   ${P4RUNTIME_BUILD_EXAMPLES}")

# =============================================================================
# Proto Compilation
# =============================================================================
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_FILE ${PROTO_DIR}/p4runtime_simple.proto)
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Create output directory
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Generated file paths
set(PROTO_SRCS ${PROTO_GEN_DIR}/p4runtime_simple.pb.cc)
set(PROTO_HDRS ${PROTO_GEN_DIR}/p4runtime_simple.pb.h)
set(GRPC_SRCS ${PROTO_GEN_DIR}/p4runtime_simple.grpc.pb.cc)
set(GRPC_HDRS ${PROTO_GEN_DIR}/p4runtime_simple.grpc.pb.h)

# Custom command to generate protobuf and gRPC code
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
        --cpp_out=${PROTO_GEN_DIR}
        --grpc_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        -I${PROTO_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating P4Runtime protobuf and gRPC code"
    VERBATIM
)

# Proto library
add_library(p4runtime_proto STATIC
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_include_directories(p4runtime_proto PUBLIC
    ${PROTO_GEN_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${GRPC_INCLUDE_DIRS}
)
target_link_libraries(p4runtime_proto PUBLIC
    ${Protobuf_LIBRARIES}
    ${GRPC_LIBRARIES}
)
# Suppress warnings in generated code
target_compile_options(p4runtime_proto PRIVATE -w)

# =============================================================================
# Runtime Engine Library (Core - no gRPC dependency)
# =============================================================================
add_library(p4runtime_engine STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime_engine.cpp
)
target_include_directories(p4runtime_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(p4runtime_engine PUBLIC
    Coyote
)

# =============================================================================
# P4Runtime Server Library
# =============================================================================
if(P4RUNTIME_BUILD_SERVER)
    add_library(p4runtime_server STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/p4runtime_server.cpp
    )
    target_include_directories(p4runtime_server PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROTO_GEN_DIR}
    )
    target_link_libraries(p4runtime_server PUBLIC
        p4runtime_engine
        p4runtime_proto
    )
endif()

# =============================================================================
# P4Runtime Client Library
# =============================================================================
if(P4RUNTIME_BUILD_CLIENT)
    add_library(p4runtime_client STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/p4runtime_client.cpp
    )
    target_include_directories(p4runtime_client PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROTO_GEN_DIR}
    )
    target_link_libraries(p4runtime_client PUBLIC
        p4runtime_proto
    )
endif()

# =============================================================================
# Combined P4Runtime Library (for convenience)
# =============================================================================
add_library(p4runtime INTERFACE)
target_link_libraries(p4runtime INTERFACE
    p4runtime_engine
    $<$<BOOL:${P4RUNTIME_BUILD_SERVER}>:p4runtime_server>
    $<$<BOOL:${P4RUNTIME_BUILD_CLIENT}>:p4runtime_client>
)

# =============================================================================
# Example Applications
# =============================================================================
if(P4RUNTIME_BUILD_EXAMPLES)
    message(STATUS "Building P4Runtime examples")

    # Server example
    if(P4RUNTIME_BUILD_SERVER)
        add_subdirectory(examples/server)
    endif()

    # Client example
    if(P4RUNTIME_BUILD_CLIENT)
        add_subdirectory(examples/client)
    endif()
endif()

# =============================================================================
# Installation
# =============================================================================
install(TARGETS
    p4runtime_engine
    p4runtime_proto
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

if(P4RUNTIME_BUILD_SERVER)
    install(TARGETS p4runtime_server ARCHIVE DESTINATION lib)
endif()

if(P4RUNTIME_BUILD_CLIENT)
    install(TARGETS p4runtime_client ARCHIVE DESTINATION lib)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include/p4runtime
    FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${PROTO_GEN_DIR}/
    DESTINATION include/p4runtime/generated
    FILES_MATCHING PATTERN "*.h"
)

# =============================================================================
# Summary
# =============================================================================
message(STATUS "")
message(STATUS "P4Runtime Build Configuration Summary")
message(STATUS "======================================")
message(STATUS "  Install prefix:   ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Library output:   ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  Binary output:    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "")
