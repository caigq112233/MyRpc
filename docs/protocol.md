# MyRpc v1 协议

所有多字节整数均为大端序。固定头长为 32 字节：

| 字段 | 字节数 |
|---|---:|
| magic（MRPC） | 4 |
| version | 2 |
| type（request=1，response=2） | 2 |
| request_id | 8 |
| status_code | 4 |
| service_len | 4 |
| method_len | 4 |
| payload_len | 4 |

其后依次拼接 service、method、payload。请求的 status_code 必须为 0；响应 payload 为成功时的 Protobuf 数据，失败时为错误说明。服务端只在缓冲区中存在完整帧时才消费数据。


enum class StatusCode {
    kOk = 0,
    kInvalidArgument,
    kUnavailable,
    kDeadlineExceeded,
    kProtocolError,
    kInternal,
};
状态码	含义	典型场景
kOk	调用成功	服务端正常执行，成功返回 Protobuf 响应
kInvalidArgument	参数不合法	Endpoint 格式错误、端口非法、请求 Protobuf 反序列化失败
kUnavailable	服务暂时不可用	ZooKeeper 找不到服务、服务端未启动、连接被拒绝、服务节点下线
kDeadlineExceeded	超时	在规定时间内未连接成功、未发完数据或未收到响应
kProtocolError	协议错误	magic 不匹配、版本不支持、帧类型非法、长度超过限制
kInternal	框架内部错误	序列化失败、socket 配置失败、服务端处理时出现未预期错误
