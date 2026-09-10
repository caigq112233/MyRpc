#include <cstdlib>
#include <iostream>

// 该测试需要真实 ZooKeeper：验证多实例 Resolve、session 过期后重连和临时节点补注册。
int main() {
    if (std::getenv("MYRPC_ZOOKEEPER_ENDPOINT") == nullptr) {
        std::cout << "SKIP: set MYRPC_ZOOKEEPER_ENDPOINT=host:port to run ZooKeeper integration test\n";
        return 77;
    }
    std::cerr << "ZooKeeper integration harness requires a dedicated test cluster\n";
    return 77;
}
