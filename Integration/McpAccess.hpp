#pragma once
#include <cymcp/http.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>

namespace cygi {
struct McpAccessOption { cymcp::LocalAccessProfile profile; const char* id; const char* label; };
inline constexpr McpAccessOption kMcpAccessOptions[] = {
    {cymcp::LocalAccessProfile::free, "free", "Free"},
    {cymcp::LocalAccessProfile::local_controlled, "local_controlled", "Local controlled"},
    {cymcp::LocalAccessProfile::token_only, "token_only", "Token only"},
    {cymcp::LocalAccessProfile::protected_access, "protected", "Protected"},
};
struct McpAccessConfig {
    cymcp::LocalAccessProfile profile = cymcp::LocalAccessProfile::free;
    bool explicit_profile = false, enabled = true;
    int port = 41823;
    McpPermission permission = McpPermission::read_only;
};
inline McpAccessConfig ReadMcpAccessConfig(const cymcp::Json& root) {
    McpAccessConfig config;
    const auto mcp = root.value("mcp", cymcp::Json::object());
    config.enabled = mcp.value("enabled", true);
    config.port = mcp.value("port", 41823);
    if (config.port < 1 || config.port > 65535) throw cymcp::Error("InvalidOptions", "Invalid MCP port");
    if (mcp.contains("permission") || root.contains("mcp_permission")) {
        const auto value = mcp.value("permission", root.value("mcp_permission", cymcp::Json(0)));
        int permission = 0;
        if (value.is_number_integer()) permission = value.get<int>();
        else if (value == "debug-control") permission = 1;
        else if (value == "shader-modification") permission = 2;
        else if (value != "read-only") throw cymcp::Error("InvalidOptions", "Invalid MCP permission");
        if (permission < 0 || permission > 2) throw cymcp::Error("InvalidOptions", "Invalid MCP permission");
        config.permission = static_cast<McpPermission>(permission);
        // A legacy explicitly saved permission remains controlled after migration.
        config.profile = cymcp::LocalAccessProfile::local_controlled;
        config.explicit_profile = true;
    }
    if (mcp.contains("access_profile")) {
        const auto id = mcp.at("access_profile").get<std::string>();
        bool found = false;
        for (const auto& option : kMcpAccessOptions)
            if (id == option.id || (id == "protected_access" && option.id == std::string("protected"))) {
                config.profile = option.profile; found = true;
            }
        if (!found) throw cymcp::Error("InvalidOptions", "Unknown local MCP access profile");
        config.explicit_profile = mcp.value("profile_explicit", true);
    }
    return config;
}
inline cymcp::Json WriteMcpAccessConfig(cymcp::Json root, const McpAccessConfig& config) {
    auto& mcp = root["mcp"];
    if (!mcp.is_object()) mcp = cymcp::Json::object();
    for (const auto& option : kMcpAccessOptions)
        if (option.profile == config.profile) mcp["access_profile"] = option.id;
    mcp["profile_explicit"] = config.explicit_profile;
    mcp["enabled"] = config.enabled; mcp["port"] = config.port;
    mcp["permission"] = static_cast<int>(config.permission);
    return root;
}
}
