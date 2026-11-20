#include "McpHttpHost.hpp"

namespace cygi {
bool McpHttpHost::Start(const McpAccessConfig& config, const std::string& token,
                        std::function<bool(const std::string&)> allowed) {
    Stop(); error_.clear();
    try {
        const auto access = cymcp::local_access_settings(config.profile);
        auto policy = access.control_operations ? cymcp::Policy(
            [allowed](const auto&, const std::string& name, const auto&) {
                return allowed(name) ? cymcp::Decision::allow : cymcp::Decision::deny;
            }) : cymcp::Policy::allow_all();
        registry_ = CreateOperations([this](const auto& name, const auto& args, const auto& ctx) {
            auto item = std::make_shared<Pending>();
            item->name = name; item->arguments = args; item->context = ctx;
            auto result = item->result.get_future();
            {
                std::lock_guard lock(mutex_);
                if (stopping_) throw cymcp::Error("Stopped", "Local MCP is stopped");
                pending_.push_back(item);
            }
            while (result.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready) {
                ctx.cancellation.check();
                if (stopping_) throw cymcp::Error("Stopped", "Local MCP is stopped");
            }
            return result.get();
        }, std::move(policy));
        mcp_ = std::make_unique<cymcp::McpServer>(*registry_, "CyGPUInspector");
        auto endpoint = [this](const auto& json, const auto& ctx) { return mcp_->handle(json, ctx); };
        if (access.require_token)
            http_ = std::make_unique<cymcp::LocalHttpServer>(token, endpoint);
        else
            http_ = std::make_unique<cymcp::LocalHttpServer>(
                cymcp::LocalHttpServer::UnauthenticatedLoopback{}, endpoint);
        stopping_ = false;
        port_ = http_->start(config.port);
        return true;
    } catch (const std::exception& e) {
        error_ = cymcp::redact_error_message(e.what()); Stop(); return false;
    }
}
void McpHttpHost::Stop() {
    stopping_ = true;
    if (mcp_) mcp_->cancel_all();
    if (http_) http_->stop();
    http_.reset(); mcp_.reset(); registry_.reset(); port_ = 0;
    std::lock_guard lock(mutex_); pending_.clear();
}
void McpHttpHost::Pump(const OperationHandler& handler) {
    std::deque<std::shared_ptr<Pending>> work;
    { std::lock_guard lock(mutex_); work.swap(pending_); }
    for (auto& item : work) {
        try {
            item->context.cancellation.check();
            item->result.set_value(handler(item->name, item->arguments, item->context));
        } catch (...) { item->result.set_exception(std::current_exception()); }
    }
}
}
