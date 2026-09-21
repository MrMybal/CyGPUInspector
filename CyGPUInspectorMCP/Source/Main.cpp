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
	struct ToolDefinition
	{
		const char *name;
		const char *description;
		const char *arguments;   // JSON schema properties, or "" for none
	};

	// Section 49 of the brief. The descriptions matter: they are what an agent reads to decide
	// what to call, so they say what the data means and what it does not.
	const ToolDefinition kTools[] = {
		{ "get_session", "What CyGPUInspector is currently looking at: the connected game or the open capture.", "" },
		{ "get_current_frame", "Counters of the frame currently held: draws, dispatches, events, CPU and GPU time.", "" },

		{ "list_shaders", "Every shader seen. Optional: query (text filter), used_this_frame (bool), limit.",
		  "\"query\":{\"type\":\"string\"},\"used_this_frame\":{\"type\":\"boolean\"},\"limit\":{\"type\":\"integer\"}" },
		{ "search_shaders", "Shaders used in the current frame matching a text query on stage, format or signature.",
		  "\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}" },
		{ "inspect_shader", "Everything known about one shader, including its reflected bindings when analysed.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "get_shader_disassembly", "The DXBC or DXIL disassembly of a shader. Starts the work if needed.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "get_shader_decompiled_hlsl", "HLSL reconstructions of a shader, one per backend, each with its validation verdict. These are reconstructions, never the original source.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "decompile_shader", "Runs every decompiler backend on a shader and returns all of their outputs.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "get_shader_draw_calls", "The indices of the draws and dispatches that ran a shader in the current frame.",
		  "\"shader_id\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\"}" },
		{ "get_shader_timing", "GPU time attributed to a shader. Needs a profiling tracking level.",
		  "\"shader_id\":{\"type\":\"integer\"}" },

		{ "list_resources", "Every resource seen. Optional: query, render_targets_only, limit.",
		  "\"query\":{\"type\":\"string\"},\"render_targets_only\":{\"type\":\"boolean\"},\"limit\":{\"type\":\"integer\"}" },
		{ "search_resources", "Resources matching a text query on kind, format or usage.",
		  "\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}" },
		{ "inspect_resource", "Everything known about one resource, including its lifetime and per frame access counts.",
		  "\"resource_id\":{\"type\":\"integer\"}" },
		{ "get_resource_writers", "The passes that wrote a resource in the analysed frame.",
		  "\"resource_id\":{\"type\":\"integer\"}" },
		{ "get_resource_readers", "The passes that read a resource in the analysed frame.",
		  "\"resource_id\":{\"type\":\"integer\"}" },

		{ "get_frame_graph", "The reconstructed passes and the dependencies between them. No game names its passes: every name is derived and carries its confidence.", "" },
		{ "get_passes", "The reconstructed passes alone, without the dependency edges.", "" },
		{ "inspect_draw", "One command of the frame: its pipeline, shaders, targets, counts and GPU time.",
		  "\"event_index\":{\"type\":\"integer\"}" },
		{ "inspect_dispatch", "Same as inspect_draw, for a compute dispatch.",
		  "\"event_index\":{\"type\":\"integer\"}" },

		{ "disable_shader", "Makes the game skip every draw that uses this shader. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "enable_shader", "Undoes disable_shader. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "highlight_shader", "Replaces a shader with a generated one that writes magenta. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "capture_frame", "Saves the current frame as a capture on disk. Needs Debug Control.", "" },
		{ "start_deep_capture", "Arms the expensive one shot capture in the game: every descriptor bound to every command, the pipeline state and the barriers, for a few frames. buffers (default true) also saves every texture the last captured frame wrote to, as PNG and DDS, in the capture's folder. Needs Debug Control.",
		  "\"frames\":{\"type\":\"integer\"},\"bindings\":{\"type\":\"boolean\"},\"barriers\":{\"type\":\"boolean\"},\"per_draw_timing\":{\"type\":\"boolean\"},\"buffers\":{\"type\":\"boolean\"}" },
		{ "get_capture_state", "Where the deep capture has got to, how much it recorded, the folder it was saved to and each saved buffer with its files.", "" },
		{ "get_frame_timeline", "The passes of the analysed frame in order, each with where it starts on the GPU, how long it lasts, its share of the frame, and whether that length was measured or placed between two measurements. This is the runtime capture, which costs the game almost nothing. include_measurements adds every raw timestamp.",
		  "\"include_measurements\":{\"type\":\"boolean\"}" },
		{ "get_command_state", "What one command was given: every descriptor on every slot, the viewport, the scissor and the fixed function state of its pipeline. Needs a deep capture to have run.",
		  "\"event_index\":{\"type\":\"integer\"}" },

		{ "compile_shader", "Compiles HLSL for the profile of a shader without injecting it. Needs Shader Modification.",
		  "\"shader_id\":{\"type\":\"integer\"},\"hlsl\":{\"type\":\"string\"}" },
		{ "replace_shader", "Compiles HLSL and injects it in place of a shader, live. Needs Shader Modification.",
		  "\"shader_id\":{\"type\":\"integer\"},\"hlsl\":{\"type\":\"string\"}" },
		{ "restore_shader", "Puts the original shader back. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
	};

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
