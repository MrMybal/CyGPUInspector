// CyGPUInspectorApp — the standalone side of the MCP server.
//
// CyGPUInspectorMCP.exe speaks MCP on stdio and forwards every tool call here over a named pipe.
// It is a proxy and nothing more: the standalone decides what is allowed, because the proxy runs
// in whatever process an agent launched it from and cannot be trusted to police itself.
//
// Requests are handled on the UI thread, not on the pipe thread: they read the session model, the
// frame graph and the analysis cache, and answering from another thread would mean locking all of
// them from the outside. One frame of latency is irrelevant for a tool call.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/ControlPipe.hpp>
#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace cygi
{
	class McpBridge
	{
	public:
		~McpBridge();

		bool Start();
		void Stop();

		bool IsRunning() const { return m_pipe.IsRunning(); }
		bool IsClientConnected() const { return m_pipe.IsClientConnected(); }

		McpPermission Permission() const { return m_permission.load(std::memory_order_relaxed); }
		void SetPermission(McpPermission permission);

		// Called once per UI frame. `handle` receives the parsed request and returns the answer.
		void Pump(const std::function<Json(const Json &request)> &handle);

		// Every call is logged, and the ones above read only are marked, so nothing an agent does
		// is invisible to the person sitting in front of the tool.
		const std::deque<std::string> &Log() const { return m_log; }
		uint64_t CallCount() const { return m_calls; }
		void ClearLog() { m_log.clear(); }

		// True when a tool of that name may run at the current permission level.
		bool IsAllowed(const std::string &tool) const;
		static McpPermission RequiredPermission(const std::string &tool);

	private:
		void Append(const std::string &line);

		ControlPipeServer m_pipe;
		std::atomic<McpPermission> m_permission{ McpPermission::read_only };
		std::deque<std::string> m_log;
		uint64_t m_calls = 0;
	};
}
