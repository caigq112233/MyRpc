#include <iostream>

// 压测入口保留为独立可执行程序：后续接入 Echo fixture 后输出 QPS、成功率、P50/P95/P99。
int main() {
    std::cout << "rpc_load_test requires an endpoint and workload arguments\n";
    return 0;
}
