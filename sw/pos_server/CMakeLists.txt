#
# POS Server and Client gRPC Service
#
# Provides gRPC infrastructure for deploying and managing DFGs on POS worker nodes.
#
# Components:
#   - POS server (Worker node): Receives deployment requests and manages DFG instances
#   - POS client (Client node): Sends deployment requests to worker nodes
#
cmake_minimum_required(VERSION 3.16)
project(pos_server VERSION 1.0.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_POS_SERVER_EXAMPLES "Build POS server/client example applications" ON)

# Find required packages
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Get gRPC plugin location
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
if(NOT GRPC_CPP_PLUGIN)
    message(FATAL_ERROR "grpc_cpp_plugin not found. Please install gRPC.")
endif()

# Proto files
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(PROTO_FILES ${PROTO_DIR}/pos_service.proto)

# Generated proto output directory
set(PROTO_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

# Generate protobuf and gRPC sources
set(PROTO_SRCS ${PROTO_GEN_DIR}/pos_service.pb.cc)
set(PROTO_HDRS ${PROTO_GEN_DIR}/pos_service.pb.h)
set(GRPC_SRCS ${PROTO_GEN_DIR}/pos_service.grpc.pb.cc)
set(GRPC_HDRS ${PROTO_GEN_DIR}/pos_service.grpc.pb.h)

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --grpc_out=${PROTO_GEN_DIR}
         --cpp_out=${PROTO_GEN_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
         -I ${PROTO_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating protobuf and gRPC sources for POS service"
)

# Create generated proto library
add_library(pos_service_proto STATIC
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(pos_service_proto PUBLIC
    ${PROTO_GEN_DIR}
)

target_link_libraries(pos_service_proto PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
)

# POS Server library (Worker node component)
add_library(pos_server_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pos_server.cpp
)

target_include_directories(pos_server_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(pos_server_lib PUBLIC
    pos_service_proto
    gRPC::grpc++
)

# POS Client library (Client node component - gRPC client)
add_library(pos_client_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pos_client.cpp
)

target_include_directories(pos_client_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(pos_client_lib PUBLIC
    pos_service_proto
    gRPC::grpc++
)

# Combined library (both server and client)
add_library(pos_grpc INTERFACE)
target_link_libraries(pos_grpc INTERFACE
    pos_server_lib
    pos_client_lib
)

# Examples
if(BUILD_POS_SERVER_EXAMPLES)
    # POS Server example (Worker node)
    add_executable(pos_server_example
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/server/main.cpp
    )
    target_link_libraries(pos_server_example PRIVATE
        pos_server_lib
    )

    # POS Client example (Client node)
    add_executable(pos_client_example
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/client/main.cpp
    )
    target_link_libraries(pos_client_example PRIVATE
        pos_client_lib
    )

    # Set output directory for examples
    set_target_properties(pos_server_example pos_client_example
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    )
endif()

# Install targets
install(TARGETS pos_service_proto pos_server_lib pos_client_lib
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include/pos_server
    FILES_MATCHING PATTERN "*.hpp"
)

install(DIRECTORY ${PROTO_GEN_DIR}/
    DESTINATION include/pos_server/proto
    FILES_MATCHING PATTERN "*.h"
)

install(FILES ${PROTO_FILES}
    DESTINATION share/pos_server/proto
)
