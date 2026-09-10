# CMake generated Testfile for 
# Source directory: /home/caigq/codes/MyRpcV2
# Build directory: /home/caigq/codes/MyRpcV2/build_integration
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(myrpc_frame_test "/home/caigq/codes/MyRpcV2/build_integration/myrpc_frame_test")
set_tests_properties(myrpc_frame_test PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/CMakeLists.txt;60;add_test;/home/caigq/codes/MyRpcV2/CMakeLists.txt;0;")
add_test(myrpc_endpoint_test "/home/caigq/codes/MyRpcV2/build_integration/myrpc_endpoint_test")
set_tests_properties(myrpc_endpoint_test PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/CMakeLists.txt;53;add_test;/home/caigq/codes/MyRpcV2/CMakeLists.txt;63;add_myrpc_unit_test;/home/caigq/codes/MyRpcV2/CMakeLists.txt;0;")
add_test(myrpc_controller_test "/home/caigq/codes/MyRpcV2/build_integration/myrpc_controller_test")
set_tests_properties(myrpc_controller_test PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/CMakeLists.txt;53;add_test;/home/caigq/codes/MyRpcV2/CMakeLists.txt;64;add_myrpc_unit_test;/home/caigq/codes/MyRpcV2/CMakeLists.txt;0;")
add_test(myrpc_status_test "/home/caigq/codes/MyRpcV2/build_integration/myrpc_status_test")
set_tests_properties(myrpc_status_test PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/home/caigq/codes/MyRpcV2/CMakeLists.txt;53;add_test;/home/caigq/codes/MyRpcV2/CMakeLists.txt;65;add_myrpc_unit_test;/home/caigq/codes/MyRpcV2/CMakeLists.txt;0;")
subdirs("tests/integration")
subdirs("tests/stress")
subdirs("examples/user_service")
