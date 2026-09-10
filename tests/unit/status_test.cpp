#include <cassert>

#include "myrpc/status.hpp"

int main() {
    const myrpc::Status ok = myrpc::Status::Ok();
    assert(ok.ok());
    assert(ok.code() == myrpc::StatusCode::kOk);
    assert(ok.message().empty());

    const myrpc::Status cancelled{myrpc::StatusCode::kCancelled, "caller cancelled"};
    assert(!cancelled.ok());
    assert(cancelled.code() == myrpc::StatusCode::kCancelled);
    assert(cancelled.message() == "caller cancelled");
}
