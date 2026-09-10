set(USER_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/user.proto)
protobuf_generate_cpp(USER_PROTO_SRCS USER_PROTO_HDRS ${USER_PROTO})

add_library(myrpc_user_proto STATIC ${USER_PROTO_SRCS} ${USER_PROTO_HDRS})
target_link_libraries(myrpc_user_proto PUBLIC protobuf::libprotobuf)
target_include_directories(myrpc_user_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

function(add_myrpc_user_example target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE myrpc::myrpc myrpc_user_proto)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_myrpc_user_example(myrpc_user_server server.cpp)
add_myrpc_user_example(myrpc_user_sync_client sync_client.cpp)
add_myrpc_user_example(myrpc_user_async_client asyn_client.cpp)
