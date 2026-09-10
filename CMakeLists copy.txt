cmake_minimum_required(VERSION 3.16)
project(MyRpc VERSION 0.1.0 LANGUAGES CXX)

option(MYRPC_BUILD_TESTS "Build MyRpc tests" ON)
option(MYRPC_BUILD_INTEGRATION_TESTS "Build MyRpc integration and stress tests" OFF)
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
    src/common/thread_pool.cpp
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

    function(add_myrpc_unit_test target)
        add_executable(${target} ${ARGN})
        target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_link_libraries(${target} PRIVATE myrpc::myrpc)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES LABELS "unit")
    endfunction()

    add_executable(myrpc_frame_test tests/unit/frame_test.cpp src/codec/frame.cpp)
    target_include_directories(myrpc_frame_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(myrpc_frame_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME myrpc_frame_test COMMAND myrpc_frame_test)
    set_tests_properties(myrpc_frame_test PROPERTIES LABELS "unit")

    add_myrpc_unit_test(myrpc_endpoint_test tests/unit/endpoint_test.cpp)
    add_myrpc_unit_test(myrpc_controller_test tests/unit/controller_test.cpp)
    add_myrpc_unit_test(myrpc_status_test tests/unit/status_test.cpp)

    if(MYRPC_BUILD_INTEGRATION_TESTS)
        add_subdirectory(tests/integration)
        add_subdirectory(tests/stress)
    endif()
endif()

if(MYRPC_BUILD_EXAMPLES)
    add_subdirectory(examples/user_service)
endif()
