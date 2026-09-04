#include <iostream>
#include <memory>

#include "myrpc/channel.hpp"
#include "myrpc/controller.hpp"
#include "myrpc/zookeeper_registry.hpp"
#include "user.pb.h"

int main() {
    auto registry = std::make_shared<myrpc::ZookeeperRegistry>("127.0.0.1:2181");
    myrpc::RpcChannel channel(registry);
    example::EchoService_Stub stub(&channel);

    example::EchoRequest request;
    request.set_message("hello MyRpc");
    example::EchoResponse response;
    myrpc::RpcController controller;
    stub.Echo(&controller, &request, &response, nullptr);
    if (controller.Failed()) {
        std::cerr << "RPC failed: " << controller.ErrorText() << '\n';
        return 1;
    }
    std::cout << response.message() << '\n';
}
