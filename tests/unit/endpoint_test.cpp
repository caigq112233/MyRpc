#include <cassert>

#include "myrpc/endpoint.hpp"

int main() {
    myrpc::Endpoint endpoint;
    assert(myrpc::Endpoint::Parse("127.0.0.1:8000", &endpoint).ok());
    assert(endpoint.host == "127.0.0.1");
    assert(endpoint.port == 8000);
    assert(endpoint.ToString() == "127.0.0.1:8000");

    assert(!myrpc::Endpoint::Parse("", &endpoint).ok());
    assert(!myrpc::Endpoint::Parse("host", &endpoint).ok());
    assert(!myrpc::Endpoint::Parse(":8000", &endpoint).ok());
    assert(!myrpc::Endpoint::Parse("host:0", &endpoint).ok());
    assert(!myrpc::Endpoint::Parse("host:65536", &endpoint).ok());
    assert(!myrpc::Endpoint::Parse("host:not-a-port", &endpoint).ok());
}
