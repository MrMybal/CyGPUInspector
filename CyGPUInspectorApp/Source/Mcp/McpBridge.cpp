// CyGPUInspectorApp — the standalone side of the MCP server.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "McpBridge.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdio>

namespace cygi
{
	namespace
	{
		// Section 52 of the brief: three levels, read only by default.
		const char *const kDebugTools[] = {
			"disable_shader", "enable_shader", "highlight_shader", "capture_frame",
			// Arming a deep capture makes the game slower for a few frames, so it is an action,
			// not a reading, and it sits behind the same permission as disabling a shader.
			"start_deep_capture",
		};
		const char *const kModificationTools[] = {
			"compile_shader", "replace_shader", "restore_shader",
		};

		std::string Timestamp()
		{
			SYSTEMTIME now = {};
			GetLocalTime(&now);

			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
			return buffer;
		}
	}

	McpBridge::~McpBridge()
	{
		Stop();
	}

	bool McpBridge::Start()
	{
		if (!m_pipe.Start(kAppPipeName))
			return false;

		Append("MCP server listening on " + std::string(kAppPipeName));
		return true;
	}

	void McpBridge::Stop()
	{
		m_pipe.Stop();
	}

	void McpBridge::SetPermission(McpPermission permission)
	{
		m_permission.store(permission, std::memory_order_relaxed);
		Append(std::string("permission level set to ") + McpPermissionName(permission));
	}

	McpPermission McpBridge::RequiredPermission(const std::string &tool)
	{
		for (const char *name : kModificationTools)
			if (tool == name)
				return McpPermission::shader_modification;
		for (const char *name : kDebugTools)
			if (tool == name)
				return McpPermission::debug_control;
		return McpPermission::read_only;
	}

	bool McpBridge::IsAllowed(const std::string &tool) const
	{
		return static_cast<uint32_t>(RequiredPermission(tool)) <=
		       static_cast<uint32_t>(m_permission.load(std::memory_order_relaxed));
	}

	void McpBridge::Append(const std::string &line)
	{
		m_log.push_back(Timestamp() + "  " + line);
		while (m_log.size() > 256)
			m_log.pop_front();
	}

	void McpBridge::Pump(const std::function<Json(const Json &request)> &handle)
	{
		ControlMessage message;
		while (m_pipe.PopMessage(message))
		{
			if (message.type != ControlType::mcp_request)
			{
				m_pipe.SendAck(message.request_id, false, "this pipe only speaks MCP");
				continue;
			}

			const std::string text(reinterpret_cast<const char *>(message.payload.data()),
				message.payload.size());

			Json request;
			std::string parse_error;
			Json response = Json::Object();

			if (!Json::Parse(text, request, &parse_error) || !request.IsObject())
			{
				response["ok"] = Json(false);
				response["error"] = Json("the request is not valid JSON: " + parse_error);
			}
			else
			{
				const std::string tool = request["tool"].AsString();
				++m_calls;

				if (!IsAllowed(tool))
				{
					const McpPermission required = RequiredPermission(tool);
					response["ok"] = Json(false);
					response["error"] = Json(std::string("\"") + tool + "\" needs the " +
						McpPermissionName(required) + " permission level, which is not granted. "
						"Change it in the MCP panel of CyGPUInspector.");
					Append("REFUSED " + tool + " (needs " + McpPermissionName(required) + ")");
				}
				else
				{
					response = handle(request);
					const bool elevated = RequiredPermission(tool) != McpPermission::read_only;
					Append(std::string(elevated ? "! " : "  ") + tool +
						(response["ok"].AsBool() ? "" : "  -> failed"));
				}
			}

			const std::string reply = response.Write(-1);
			m_pipe.Send(ControlType::mcp_response, message.request_id, reply.data(),
				static_cast<uint32_t>(reply.size()));
		}
	}
}
