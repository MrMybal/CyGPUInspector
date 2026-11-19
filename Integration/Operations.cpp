#include "Operations.hpp"
#include <CyGPUInspectorCore/AutomationTools.hpp>
#include <CyGPUInspectorCore/McpTools.hpp>
namespace cygi {
std::shared_ptr<cymcp::Registry> CreateOperations(OperationHandler handler) {
  auto registry = std::make_shared<cymcp::Registry>(cymcp::Policy::allow_all());
  for (const auto &tool : kMcpTools) {
    cymcp::Operation op;
    op.name = tool.name;
    op.description = tool.description;
    op.input_schema =
        cymcp::parse_json(std::string("{\"type\":\"object\",\"properties\":{") +
                          tool.arguments + "},\"additionalProperties\":false}");
    auto required = cymcp::Json::array();
    for (const char *key :
         {"shader_id", "resource_id", "event_index", "hlsl", "a", "b"})
      if (op.input_schema["properties"].contains(key))
        required.push_back(key);
    if (!required.empty())
      op.input_schema["required"] = required;
    op.output_schema = {{"type", "object"},
                        {"properties",
                         {{"ok", {{"type", "boolean"}}},
                          {"result", cymcp::Json::object()},
                          {"error", {{"type", "string"}}}}},
                        {"required", {"ok"}}};
    // All dispatches are uncached. Existing analysis commands can enqueue
    // background work.
    op.read_only = true;
    for (const char *action :
         {"disable_shader", "enable_shader", "highlight_shader",
          "capture_frame", "start_deep_capture", "compile_shader",
          "replace_shader", "restore_shader"})
      if (op.name == action)
        op.read_only = false;
    const auto name = op.name;
    op.handler = [handler, name](const auto &args, const auto &ctx) {
      ctx.cancellation.check();
      return handler(name, args, ctx);
    };
    registry->add(std::move(op));
  }
  for (const auto &t : kAutomationTools) {
    cymcp::Operation op;
    op.name = t.name;
    op.description = t.description;
    op.input_schema = {{"type", "object"},
                       {"properties", cymcp::parse_json(t.properties)},
                       {"required", cymcp::parse_json(t.required)},
                       {"additionalProperties", false}};
    op.output_schema = {{"type", "object"},
                        {"required", {"ok"}},
                        {"properties",
                         {{"ok", {{"type", "boolean"}}},
                          {"result", cymcp::Json::object()},
                          {"error", {{"type", "string"}}}}}};
    op.read_only = !t.mutates;
    const auto name = op.name;
    op.handler = [handler, name](const auto &args, const auto &ctx) {
      ctx.cancellation.check();
      return handler(name, args, ctx);
    };
    registry->add(std::move(op));
  }
  return registry;
}
} // namespace cygi
