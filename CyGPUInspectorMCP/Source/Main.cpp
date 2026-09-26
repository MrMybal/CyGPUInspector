// CyGPUInspectorMCP — the MCP server an agent launches.
//
// It speaks MCP (JSON-RPC over stdio, one message per line) and forwards every tool call to
// CyGPUInspectorApp over a named pipe. It holds no data and takes no decision: the standalone owns
// the session and the permission level, because this process runs wherever an agent started it.
//
// Usage: point an MCP client at this executable. Nothing to configure; it finds the standalone by
// its pipe and says so plainly when the standalone is not running.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include <CyGPUInspectorCore/ControlPipe.hpp>
#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/McpTools.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using ToolDefinition = cygi::McpToolDefinition;
	constexpr const auto &kTools = cygi::kMcpTools;

	void WriteMessage(const cygi::Json &message)
	{
		// MCP stdio framing: one JSON object per line, nothing else on stdout ever.
		const std::string text = message.Write(-1);
		std::fwrite(text.data(), 1, text.size(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}

	void WriteError(const cygi::Json &id, int code, const std::string &message)
	{
		cygi::Json error = cygi::Json::Object();
		error["code"] = cygi::Json(code);
		error["message"] = cygi::Json(message);

		cygi::Json response = cygi::Json::Object();
		response["jsonrpc"] = cygi::Json("2.0");
		response["id"] = id;
		response["error"] = error;
		WriteMessage(response);
	}

	void WriteResult(const cygi::Json &id, cygi::Json result)
	{
		cygi::Json response = cygi::Json::Object();
		response["jsonrpc"] = cygi::Json("2.0");
		response["id"] = id;
		response["result"] = std::move(result);
		WriteMessage(response);
	}

	// Wraps a payload as MCP tool content: a single text block holding the JSON answer.
	cygi::Json TextContent(const std::string &text, bool is_error)
	{
		cygi::Json block = cygi::Json::Object();
		block["type"] = cygi::Json("text");
		block["text"] = cygi::Json(text);

		cygi::Json content = cygi::Json::Array();
		content.Push(block);

		cygi::Json result = cygi::Json::Object();
		result["content"] = content;
		result["isError"] = cygi::Json(is_error);
		return result;
	}

	class AppLink
	{
	public:
		bool EnsureConnected()
		{
			if (m_pipe.IsConnected())
				return true;
			return m_pipe.Connect(cygi::kAppPipeName, 500);
		}

		// Returns the raw JSON answer, or an explanation of why there is none.
		bool Call(const std::string &tool, const cygi::Json &arguments, std::string &out)
		{
			if (!EnsureConnected())
			{
				out = "CyGPUInspectorApp is not running, or its MCP server is not listening. "
				      "Start the standalone and try again.";
				return false;
			}

			cygi::Json request = cygi::Json::Object();
			request["tool"] = cygi::Json(tool);
			request["arguments"] = arguments;

			const std::string text = request.Write(-1);
			const uint64_t id = m_pipe.NextRequestId();
			if (!m_pipe.Send(cygi::ControlType::mcp_request, id, text.data(),
			                 static_cast<uint32_t>(text.size())))
			{
				m_pipe.Disconnect();
				out = "the request could not be sent to CyGPUInspectorApp";
				return false;
			}

			cygi::ControlMessage reply;
			for (int attempt = 0; attempt < 4; ++attempt)
			{
				// The standalone answers on its UI thread, so one frame of latency is normal.
				if (!m_pipe.Receive(reply, 5000))
				{
					m_pipe.Disconnect();
					out = "CyGPUInspectorApp did not answer within five seconds";
					return false;
				}
				if (reply.request_id == id)
					break;
			}

			out.assign(reinterpret_cast<const char *>(reply.payload.data()), reply.payload.size());
			return true;
		}

	private:
		cygi::ControlPipeClient m_pipe;
	};
}

int main()
{
	AppLink link;

	std::string line;
	while (std::getline(std::cin, line))
	{
		if (line.empty())
			continue;

		cygi::Json message;
		std::string parse_error;
		if (!cygi::Json::Parse(line, message, &parse_error) || !message.IsObject())
		{
			WriteError(cygi::Json(), -32700, "parse error: " + parse_error);
			continue;
		}

		const std::string method = message["method"].AsString();
		const cygi::Json &id = message["id"];
		const bool is_notification = !message.Has("id");

		if (method == "initialize")
		{
			cygi::Json capabilities = cygi::Json::Object();
			capabilities["tools"] = cygi::Json::Object();

			cygi::Json info = cygi::Json::Object();
			info["name"] = cygi::Json("CyGPUInspectorMCP");
			info["version"] = cygi::Json(cygi::kVersionString);

			cygi::Json result = cygi::Json::Object();
			result["protocolVersion"] = cygi::Json("2024-11-05");
			result["capabilities"] = capabilities;
			result["serverInfo"] = info;
			result["instructions"] = cygi::Json(
				"CyGPUInspector inspects the GPU frame of a game running ReShade. Start with "
				"get_session and get_frame_graph, then follow resources and shaders. Pass names are "
				"derived from resource dependencies, not read from the game, so each carries a "
				"confidence. Decompiled HLSL is a reconstruction: the original source does not exist "
				"in a compiled shader. Actions that change what the game renders require a higher "
				"permission level, granted by the person in front of the tool.");

			WriteResult(id, std::move(result));
			continue;
		}

		if (method == "notifications/initialized" || method == "notifications/cancelled")
			continue;

		if (method == "ping")
		{
			WriteResult(id, cygi::Json::Object());
			continue;
		}

		if (method == "tools/list")
		{
			cygi::Json tools = cygi::Json::Array();
			for (const ToolDefinition &definition : kTools)
			{
				cygi::Json schema;
				const std::string text = std::string("{\"type\":\"object\",\"properties\":{") +
					definition.arguments + "}}";
				cygi::Json::Parse(text, schema);

				cygi::Json entry = cygi::Json::Object();
				entry["name"] = cygi::Json(definition.name);
				entry["description"] = cygi::Json(definition.description);
				entry["inputSchema"] = schema;
				tools.Push(entry);
			}

			cygi::Json result = cygi::Json::Object();
			result["tools"] = tools;
			WriteResult(id, std::move(result));
			continue;
		}

		if (method == "tools/call")
		{
			const std::string tool = message["params"]["name"].AsString();
			const cygi::Json &arguments = message["params"]["arguments"];

			std::string answer;
			if (!link.Call(tool, arguments, answer))
			{
				WriteResult(id, TextContent(answer, true));
				continue;
			}

			cygi::Json parsed;
			if (!cygi::Json::Parse(answer, parsed) || !parsed.IsObject())
			{
				WriteResult(id, TextContent("CyGPUInspectorApp returned something unreadable", true));
				continue;
			}

			const bool ok = parsed["ok"].AsBool();
			const cygi::Json &payload = ok ? parsed["result"] : parsed["error"];
			WriteResult(id, TextContent(ok ? payload.Write(2) : payload.AsString(), !ok));
			continue;
		}

		if (!is_notification)
			WriteError(id, -32601, "unknown method \"" + method + "\"");
	}

	return 0;
}
