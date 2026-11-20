#pragma once
#include <cymcp/core.hpp>
#include <functional>
namespace cygi {
using OperationHandler = std::function<cymcp::Json(const std::string&, const cymcp::Json&, const cymcp::CallContext&)>;
std::shared_ptr<cymcp::Registry> CreateOperations(OperationHandler handler,
    cymcp::Policy policy = cymcp::Policy::allow_all());
}
