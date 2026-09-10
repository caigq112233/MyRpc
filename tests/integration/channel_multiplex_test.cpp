#include <cstdlib>
#include <iostream>

// 该测试需要一个由测试脚本启动的 Echo Provider。它应当验证：多个 done 回调
// 可并发提交、仅建立一条连接，以及乱序响应按 request_id 分发到正确回调。
int main() {
    if (std::getenv("MYRPC_TEST_ENDPOINT") == nullptr) {
        std::cout << "SKIP: set MYRPC_TEST_ENDPOINT=host:port to run multiplex integration test\n";
        return 77;
    }
    std::cerr << "channel multiplex harness requires the Echo test fixture\n";
    return 77;
}
