# MyRpc

一个基于 Protobuf、Muduo 和 ZooKeeper 的 C++17 RPC 框架。

## 已实现的核心

- 二进制帧协议：magic、version、type、request_id、status、lengths 和 data。
- 所有整数采用网络字节序；限制元数据为 8 KiB、消息体为 16 MiB。
- FrameParser/FrameCodec 支持半包、粘包和二进制 Protobuf 内容。
- 同步客户端具备完整发送、循环接收、socket 超时和轮询选址。
- Muduo 服务端按完整帧分发请求，并通过 RAII 释放请求、响应和 Controller。
- ZooKeeper 使用多实例临时顺序节点，允许多个服务实例同时注册。

## 构建

依赖：C++17、Protobuf、Muduo、ZooKeeper C client（zookeeper_mt）和 pthread。

    cmake -S . -B build -DMYRPC_BUILD_TESTS=ON
    cmake --build build -j
    ctest --test-dir build --output-on-failure

## 构建调试
    cmake -S . -B build_debug -DCMAKE_BUILD_TYPE=Debug
    cmake --build build_debug -j

## 目前边界

当前客户端为每次调用一条短连接的同步实现；协议已经包含 request_id，下一步可加入连接池、异步 future 和多路复用。ZooKeeper 会话过期后的服务重新注册、TLS/鉴权、指标与重试策略仍待实现。
