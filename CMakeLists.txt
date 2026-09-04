cmake_minimum_required(VERSION 3.16)
project(MyRpc VERSION 0.1.0 LANGUAGES CXX)

option(MYRPC_BUILD_TESTS "Build MyRpc tests" ON)
option(MYRPC_BUILD_EXAMPLES "Build MyRpc examples" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_path(MUDUO_INCLUDE_DIR muduo/net/TcpServer.h REQUIRED)
find_library(MUDUO_NET_LIBRARY muduo_net REQUIRED)
find_library(MUDUO_BASE_LIBRARY muduo_base REQUIRED)
find_path(ZOOKEEPER_INCLUDE_DIR zookeeper/zookeeper.h REQUIRED)
find_library(ZOOKEEPER_LIBRARY zookeeper_mt REQUIRED)

add_library(myrpc STATIC
    src/client/channel.cpp
    src/codec/frame.cpp
    src/common/controller.cpp
    src/common/endpoint.cpp
    src/registry/zookeeper_registry.cpp
    src/server/provider.cpp
)
add_library(myrpc::myrpc ALIAS myrpc)

target_include_directories(myrpc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${MUDUO_INCLUDE_DIR}
        ${ZOOKEEPER_INCLUDE_DIR}
)
target_link_libraries(myrpc
    PUBLIC protobuf::libprotobuf
    PRIVATE ${MUDUO_NET_LIBRARY} ${MUDUO_BASE_LIBRARY} ${ZOOKEEPER_LIBRARY} Threads::Threads
)
target_compile_options(myrpc PRIVATE -Wall -Wextra -Wpedantic)

if(MYRPC_BUILD_TESTS)
    enable_testing()
    add_executable(myrpc_frame_test tests/unit/frame_test.cpp src/codec/frame.cpp)
    target_include_directories(myrpc_frame_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(myrpc_frame_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME myrpc_frame_test COMMAND myrpc_frame_test)
endif()

if(MYRPC_BUILD_EXAMPLES)
    add_subdirectory(examples/user_service)
endif()
