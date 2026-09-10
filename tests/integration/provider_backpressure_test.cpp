#include <cstdlib>
#include <iostream>

// 该测试需要慢读客户端 fixture：服务端写缓冲达到高水位后应 stopRead，排空后 startRead。
int main() {
    if (std::getenv("MYRPC_TEST_ENDPOINT") == nullptr) {
        std::cout << "SKIP: set MYRPC_TEST_ENDPOINT=host:port to run backpressure integration test\n";
        return 77;
    }
    std::cerr << "provider backpressure harness requires the slow-client test fixture\n";
    return 77;
}
