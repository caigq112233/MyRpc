function(add_myrpc_stress_target target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE myrpc::myrpc)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_myrpc_stress_target(myrpc_rpc_load_test rpc_load_test.cpp)
add_myrpc_stress_target(myrpc_slow_client_test slow_client_test.cpp)
