# MyRpc

一个基于 Protobuf、Muduo 和 ZooKeeper 的 C++17 RPC V1框架重构版。

## 已实现的核心

- 二进制帧协议：magic、version、type、request_id、status、timeout_ms、lengths 和 data；支持 cancel 帧。
- 客户端超时仅由 `RpcController(int timeout_ms)` 配置：该预算覆盖服务发现、建连、发送和接收；`0` 表示客户端无限等待，帧中传 `timeout_ms = 0`，由服务端使用自己的上限。
- 所有整数采用网络字节序；限制元数据为 8 KiB、消息体为 16 MiB。
- FrameParser/FrameCodec 支持半包、粘包和二进制 Protobuf 内容。
- 客户端按 Endpoint 长连接复用；接收线程通过 request_id 分发响应，Protobuf done 回调可异步并发调用。
- Muduo 服务端按完整帧分发请求；每连接维护 in-flight 调用表，deadline/取消采用协作式退出，资源由 shared_ptr + RAII 在业务结束后释放，并以高水位回压限制慢客户端写缓冲。
- ZooKeeper 使用多实例临时顺序节点，允许多个服务实例同时注册。

## 构建与运行

依赖：C++17、Protobuf、Muduo、ZooKeeper C client（zookeeper_mt）和 pthread。

构建框架、示例和默认单元测试：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMYRPC_BUILD_TESTS=ON \
  -DMYRPC_BUILD_EXAMPLES=ON
cmake --build build -j
```

示例生成的可执行文件：

```text
myrpc_user_server
myrpc_user_sync_client
myrpc_user_async_client
```

启动 ZooKeeper 后，先启动 `myrpc_user_server`，再运行同步或异步客户端。

## 测试

运行默认单元测试：

```bash
ctest --test-dir build --output-on-failure
```

也可以按标签只运行单元测试：

```bash
ctest --test-dir build -L unit --output-on-failure
```

当前单元测试覆盖：

| 测试目标 | 覆盖内容 |
|---|---|
| `myrpc_frame_test` | v2 帧编解码、网络字节序、半包、粘包、`timeout_ms`、Cancel 帧、非法请求状态码 |
| `myrpc_endpoint_test` | `host:port` 解析、端口边界与非法 Endpoint |
| `myrpc_controller_test` | 状态设置/重置、取消回调、deadline 过期 |
| `myrpc_status_test` | `Status` 与 `kCancelled` 等状态码语义 |

### 集成测试

集成测试依赖真实 Provider、可控端口和 ZooKeeper，因此默认不构建。需要时显式开启：

```bash
cmake -S . -B build_integration \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMYRPC_BUILD_TESTS=ON \
  -DMYRPC_BUILD_INTEGRATION_TESTS=ON \
  -DMYRPC_BUILD_EXAMPLES=ON
cmake --build build_integration -j
ctest --test-dir build_integration -L integration --output-on-failure
```

集成测试入口及其目标场景：

| 测试目标 | 目标场景 |
|---|---|
| `myrpc_channel_multiplex_test` | 同一连接并发请求、乱序响应与 `request_id` 匹配 |
| `myrpc_provider_deadline_test` | deadline、Cancel、`controller->IsCanceled()` 与只响应一次 |
| `myrpc_provider_backpressure_test` | 慢客户端、高水位暂停读取与写完成恢复读取 |
| `myrpc_zookeeper_registry_test` | 多实例发现、ZooKeeper session 恢复和临时节点补注册 |

未配置外部测试环境时，集成测试返回 CTest 跳过码 `77`，不会被视为通过或失败。ZooKeeper 测试需要设置：

```bash
export MYRPC_ZOOKEEPER_ENDPOINT=127.0.0.1:2181
```

网络相关集成测试还需要由测试脚本启动专用 Echo/慢请求 Provider，并设置：

```bash
export MYRPC_TEST_ENDPOINT=127.0.0.1:8000
```

### 压测入口

开启 `MYRPC_BUILD_INTEGRATION_TESTS=ON` 后还会构建两个独立压测程序：

```text
myrpc_rpc_load_test     # 统计 QPS、成功率、P50/P95/P99
myrpc_slow_client_test  # 模拟慢读客户端，验证高水位背压
```

它们不属于默认 `ctest`；需要结合目标 Endpoint 和工作负载参数运行。

## 目前边界

客户端为每 Endpoint 一条长连接的异步多路复用实现；协议以 request_id 关联并发响应。后续可扩展多连接池、future API、指标与重试策略。
