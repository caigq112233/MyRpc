# CMake generated Testfile for 
# Source directory: /home/caigq/codes/MyRpcV2/tests/integration
# Build directory: /home/caigq/codes/MyRpcV2/build_integration/tests/integration
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(myrpc_channel_multiplex_test "/home/caigq/codes/MyRpcV2/build_integration/tests/integration/myrpc_channel_multiplex_test")
set_tests_properties(myrpc_channel_multiplex_test PROPERTIES  LABELS "integration" SKIP_RETURN_CODE "77" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;6;add_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;12;add_myrpc_integration_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;0;")
add_test(myrpc_provider_deadline_test "/home/caigq/codes/MyRpcV2/build_integration/tests/integration/myrpc_provider_deadline_test")
set_tests_properties(myrpc_provider_deadline_test PROPERTIES  LABELS "integration" SKIP_RETURN_CODE "77" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;6;add_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;13;add_myrpc_integration_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;0;")
add_test(myrpc_provider_backpressure_test "/home/caigq/codes/MyRpcV2/build_integration/tests/integration/myrpc_provider_backpressure_test")
set_tests_properties(myrpc_provider_backpressure_test PROPERTIES  LABELS "integration" SKIP_RETURN_CODE "77" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;6;add_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;14;add_myrpc_integration_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;0;")
add_test(myrpc_zookeeper_registry_test "/home/caigq/codes/MyRpcV2/build_integration/tests/integration/myrpc_zookeeper_registry_test")
set_tests_properties(myrpc_zookeeper_registry_test PROPERTIES  LABELS "integration" SKIP_RETURN_CODE "77" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;6;add_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;15;add_myrpc_integration_test;/home/caigq/codes/MyRpcV2/tests/integration/CMakeLists.txt;0;")
