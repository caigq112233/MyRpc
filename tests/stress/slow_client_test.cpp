#include <iostream>

// 慢客户端压测入口：后续持续写请求但延迟读取响应，用于验证服务端高水位背压。
int main() {
    std::cout << "slow_client_test requires an endpoint and workload arguments\n";
    return 0;
}
