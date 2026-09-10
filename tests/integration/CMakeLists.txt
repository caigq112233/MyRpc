function(add_myrpc_integration_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE myrpc::myrpc)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "integration"
        SKIP_RETURN_CODE 77)
endfunction()

add_myrpc_integration_test(myrpc_channel_multiplex_test channel_multiplex_test.cpp)
add_myrpc_integration_test(myrpc_provider_deadline_test provider_deadline_test.cpp)
add_myrpc_integration_test(myrpc_provider_backpressure_test provider_backpressure_test.cpp)
add_myrpc_integration_test(myrpc_zookeeper_registry_test zookeeper_registry_test.cpp)
