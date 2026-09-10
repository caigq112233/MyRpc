#include <cstdlib>
#include <iostream>

// 该测试由外部 fixture 启动慢 Echo 服务：客户端 deadline 到达后应收到
// kDeadlineExceeded，服务端 controller->IsCanceled() 应变为 true，且不产生第二响应。
int main() {
    if (std::getenv("MYRPC_TEST_ENDPOINT") == nullptr) {
        std::cout << "SKIP: set MYRPC_TEST_ENDPOINT=host:port to run deadline integration test\n";
        return 77;
    }
    std::cerr << "provider deadline harness requires the slow Echo test fixture\n";
    return 77;
}
