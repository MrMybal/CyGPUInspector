#pragma once
#include "Operations.hpp"
#include "McpAccess.hpp"
#include <cymcp/mcp.hpp>
#include <atomic>
#include <deque>
#include <future>

namespace cygi {
// HTTP workers enqueue typed operations; only Pump accesses the application model.
class McpHttpHost {
    struct Pending {
        std::string name;
        cymcp::Json arguments;
        cymcp::CallContext context;
        std::promise<cymcp::Json> result;
    };
    std::atomic_bool stopping_{true};
    std::mutex mutex_;
    std::deque<std::shared_ptr<Pending>> pending_;
    std::shared_ptr<cymcp::Registry> registry_;
    std::unique_ptr<cymcp::McpServer> mcp_;
    std::unique_ptr<cymcp::LocalHttpServer> http_;
    int port_ = 0;
    std::string error_;
public:
    ~McpHttpHost() { Stop(); }
    bool Start(const McpAccessConfig&, const std::string& token,
               std::function<bool(const std::string&)> allowed);
    void Stop();
    void Pump(const OperationHandler&);
    int Port() const { return port_; }
    const std::string& Error() const { return error_; }
};
}
