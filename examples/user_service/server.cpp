#include <iostream>
#include <memory>

#include "myrpc/endpoint.hpp"
#include "myrpc/provider.hpp"
#include "myrpc/zookeeper_registry.hpp"
#include "user.pb.h"

class EchoServiceImpl final : public example::EchoService {
public:
    void Echo(google::protobuf::RpcController*, const example::EchoRequest* request,
              example::EchoResponse* response, google::protobuf::Closure* done) override {
        response->set_message("echo: " + request->message());
        done->Run();
    }
};

int main() {
    auto registry = std::make_shared<myrpc::ZookeeperRegistry>("127.0.0.1:2181");
    myrpc::Endpoint endpoint{"127.0.0.1", 8000};
    myrpc::RpcProvider provider(endpoint, registry);
    EchoServiceImpl service;
    provider.NotifyService(&service);

    const myrpc::Status status = provider.Run();
    if (!status.ok()) {
        std::cerr << "server failed: " << status.message() << '\n';
        return 1;
    }
}
