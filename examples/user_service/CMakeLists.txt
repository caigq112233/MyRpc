set(USER_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/user.proto)
protobuf_generate_cpp(USER_PROTO_SRCS USER_PROTO_HDRS ${USER_PROTO})

add_library(myrpc_user_proto STATIC ${USER_PROTO_SRCS} ${USER_PROTO_HDRS})
target_link_libraries(myrpc_user_proto PUBLIC protobuf::libprotobuf)
target_include_directories(myrpc_user_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

add_executable(myrpc_user_server server.cpp)
target_link_libraries(myrpc_user_server PRIVATE myrpc::myrpc myrpc_user_proto)
target_include_directories(myrpc_user_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_executable(myrpc_user_client client.cpp)
target_link_libraries(myrpc_user_client PRIVATE myrpc::myrpc myrpc_user_proto)
target_include_directories(myrpc_user_client PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
