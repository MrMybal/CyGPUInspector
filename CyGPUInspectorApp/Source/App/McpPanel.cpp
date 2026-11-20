#include "Application.hpp"
#include <CyGPUInspectorCore/Localization.hpp>
#include <imgui.h>

namespace cygi {
using i18n::Tr;
using i18n::TrId;
void Application::LoadMcpPreferences() {
    try {
        const auto root = cymcp::parse_json(LoadPreferences().Write(-1));
        m_mcp_access = ReadMcpAccessConfig(root);
        if (!root.contains("mcp") || !root["mcp"].contains("access_profile")) SaveMcpPreferences();
    } catch (const std::exception& e) {
        // Preserve unrecognized settings rather than turning an explicit selection into Free.
        m_mcp_config_error = cymcp::redact_error_message(e.what());
        m_mcp_access.enabled = false;
        m_mcp_access.profile = cymcp::LocalAccessProfile::protected_access;
        m_mcp_access.explicit_profile = true;
    }
    m_mcp.SetPermission(m_mcp_access.permission);
    m_mcp.SetUnrestricted(!cymcp::local_access_settings(m_mcp_access.profile).control_operations);
}
void Application::SaveMcpPreferences() {
    const auto root = WriteMcpAccessConfig(cymcp::parse_json(LoadPreferences().Write(-1)), m_mcp_access);
    Json saved; Json::Parse(root.dump(), saved); SavePreferences(saved);
}
void Application::SetMcpPermission(McpPermission permission) {
    m_mcp_access.permission = permission;
    if (!m_mcp_access.explicit_profile)
        m_mcp_access.profile = cymcp::LocalAccessProfile::local_controlled;
    m_mcp.SetPermission(permission);
    m_mcp.SetUnrestricted(!cymcp::local_access_settings(m_mcp_access.profile).control_operations);
}
void Application::StartMcp() {
    m_mcp_started = true;
    m_mcp.Start(); // Preserve the existing stdio/CLI companion transport.
    ApplyMcpAccess();
}
void Application::ApplyMcpAccess() {
    m_mcp_http.Stop();
    m_mcp.SetPermission(m_mcp_access.permission);
    const auto access = cymcp::local_access_settings(m_mcp_access.profile);
    m_mcp.SetUnrestricted(!access.control_operations);
    if (access.require_token && m_mcp_token.empty()) m_mcp_token = cymcp::random_token(32);
    if (!m_mcp_started || !m_mcp_access.enabled) return;
    m_mcp_http.Start(m_mcp_access, m_mcp_token, [this](const std::string& name) { return m_mcp.IsAllowed(name); });
}
void Application::DrawMcpPanel() {
    if (!BeginPanel("MCP")) { ImGui::End(); return; }
    cyui::sectionTitle(Tr("Local HTTP MCP"));
    bool enabled = m_mcp_access.enabled;
    if (ImGui::Checkbox(TrId("Enable local HTTP MCP"), &enabled)) {
        m_mcp_access.enabled = enabled; m_mcp_config_error.clear();
        ApplyMcpAccess(); SaveMcpPreferences();
    }
    if (m_mcp_http.Port()) {
        const auto url = "http://127.0.0.1:" + std::to_string(m_mcp_http.Port()) + "/mcp";
        ImGui::TextUnformatted(url.c_str()); ImGui::SameLine();
        if (ImGui::SmallButton(TrId("Copy address"))) ImGui::SetClipboardText(url.c_str());
    }
    if (!m_mcp_config_error.empty()) ImGui::TextWrapped("%s", m_mcp_config_error.c_str());
    if (!m_mcp_http.Error().empty()) ImGui::TextWrapped("%s", m_mcp_http.Error().c_str());
    int selected = static_cast<int>(m_mcp_access.profile);
    const char* labels[] = { Tr("Free"), Tr("Local controlled"), Tr("Token only"), Tr("Protected") };
    if (ImGui::Combo(TrId("Access profile"), &selected, labels, 4)) {
        m_mcp_access.profile = kMcpAccessOptions[selected].profile;
        m_mcp_access.explicit_profile = true; m_mcp_config_error.clear();
        ApplyMcpAccess(); SaveMcpPreferences();
    }
    const auto access = cymcp::local_access_settings(m_mcp_access.profile);
    if (access.require_token) {
        ImGui::TextWrapped(Tr("A token is required. It is generated for this application session and is not saved."));
        if (ImGui::Button(TrId("Copy session token"))) ImGui::SetClipboardText(m_mcp_token.c_str());
        ImGui::SameLine();
        if (ImGui::Button(TrId("New token"))) { m_mcp_token = cymcp::random_token(32); ApplyMcpAccess(); }
    } else ImGui::TextWrapped(Tr("Connect using the address alone. No token is required."));
    if (!access.control_operations)
        ImGui::TextWrapped(Tr("All registered operations are allowed without confirmation. Arguments are still validated."));
    else {
        const char* levels[] = { Tr("Read Only"), Tr("Debug Control"), Tr("Shader Modification") };
        int level = static_cast<int>(m_mcp_access.permission);
        if (ImGui::Combo(TrId("Permission"), &level, levels, 3)) {
            m_mcp_access.permission = static_cast<McpPermission>(level);
            ApplyMcpAccess(); SaveMcpPreferences();
        }
    }
    cyui::sectionTitle(Tr("Stdio and CLI"));
    ImGui::TextWrapped(Tr("CyGPUInspectorMCP.exe remains available for stdio clients and CLI scripts. Controlled profiles use the permission level above. HTTP tokens apply to HTTP connections."));
    ImGui::Text(Tr("Server: %s"), m_mcp.IsRunning() ? "listening" : "stopped");
    ImGui::Text(Tr("Proxy connected: %s"), m_mcp.IsClientConnected() ? "yes" : "no");
    cyui::sectionTitle(Tr("Activity"));
    if (ImGui::SmallButton(TrId("Clear"))) m_mcp.ClearLog();
    DrawMcpLog(); ImGui::End();
}
}
