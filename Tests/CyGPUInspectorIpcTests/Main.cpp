// CyGPUInspectorIpcTests — end to end check of the RS -> App path, without a game.
//
// Starts CyGPUInspectorFakeSession, discovers it through the session directory exactly like the
// standalone does, connects with the real SessionClient / SessionModel code, and verifies that
// shaders, pipelines, resources and frames arrive intact and that a control command is answered.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Analysis/FrameGraph.hpp"
#include "Analysis/FrameTimeline.hpp"
#include "Analysis/FrameTrack.hpp"
#include "Mcp/McpBridge.hpp"
#include "Session/CaptureArchive.hpp"
#include "Render/SharedTexture.hpp"
#include "Session/SessionClient.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11.h>

#include <CyGPUInspectorCore/InProcessApi.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace
{
	int g_failures = 0;
	int g_checks = 0;

	void Check(bool condition, const char *what)
	{
		++g_checks;
		std::printf("  %-5s %s\n", condition ? "ok" : "FAIL", what);
		if (!condition)
			++g_failures;
	}

	// Waits until `predicate` holds or the timeout expires.
	template <typename Predicate>
	bool WaitFor(Predicate predicate, uint32_t timeout_ms)
	{
		const DWORD deadline = GetTickCount() + timeout_ms;
		while (GetTickCount() < deadline)
		{
			if (predicate())
				return true;
			Sleep(25);
		}
		return predicate();
	}
}

namespace
{
	// Drives CyGPUInspectorMCP.exe the way an MCP client does: one JSON object per line on stdio.
	class McpProcess
	{
	public:
		bool Start(const std::filesystem::path &executable)
		{
			SECURITY_ATTRIBUTES attributes = { sizeof(attributes), nullptr, TRUE };

			HANDLE child_stdin_read = nullptr;
			HANDLE child_stdout_write = nullptr;
			if (!CreatePipe(&child_stdin_read, &m_stdin, &attributes, 0) ||
			    !CreatePipe(&m_stdout, &child_stdout_write, &attributes, 0))
				return false;

			SetHandleInformation(m_stdin, HANDLE_FLAG_INHERIT, 0);
			SetHandleInformation(m_stdout, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW startup = { sizeof(startup) };
			startup.dwFlags = STARTF_USESTDHANDLES;
			startup.hStdInput = child_stdin_read;
			startup.hStdOutput = child_stdout_write;
			startup.hStdError = child_stdout_write;

			std::wstring command = L"\"" + executable.wstring() + L"\"";
			const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startup, &m_process);

			CloseHandle(child_stdin_read);
			CloseHandle(child_stdout_write);
			return started != 0;
		}

		void Stop()
		{
			if (m_stdin != nullptr) { CloseHandle(m_stdin); m_stdin = nullptr; }
			if (m_process.hProcess != nullptr)
			{
				WaitForSingleObject(m_process.hProcess, 2000);
				TerminateProcess(m_process.hProcess, 0);
				CloseHandle(m_process.hThread);
				CloseHandle(m_process.hProcess);
				m_process = {};
			}
			if (m_stdout != nullptr) { CloseHandle(m_stdout); m_stdout = nullptr; }
		}

		bool Send(const std::string &line)
		{
			const std::string text = line + "\n";
			DWORD written = 0;
			return WriteFile(m_stdin, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
		}

		// Reads one line. Blocks until the process writes it or the pipe closes.
		bool ReadLine(std::string &out)
		{
			out.clear();
			for (;;)
			{
				if (m_buffer_cursor >= m_buffer.size())
				{
					char chunk[4096];
					DWORD read = 0;
					if (!ReadFile(m_stdout, chunk, sizeof(chunk), &read, nullptr) || read == 0)
						return !out.empty();
					m_buffer.assign(chunk, chunk + read);
					m_buffer_cursor = 0;
				}

				while (m_buffer_cursor < m_buffer.size())
				{
					const char c = m_buffer[m_buffer_cursor++];
					if (c == '\n')
						return true;
					if (c != '\r')
						out += c;
				}
			}
		}

		bool Call(const std::string &request, cygi::Json &response)
		{
			if (!Send(request))
				return false;
			std::string line;
			if (!ReadLine(line))
				return false;
			return cygi::Json::Parse(line, response);
		}

	private:
		HANDLE m_stdin = nullptr;
		HANDLE m_stdout = nullptr;
		PROCESS_INFORMATION m_process = {};
		std::string m_buffer;
		size_t m_buffer_cursor = 0;
	};

	void TestMcpServer(const std::filesystem::path &executable)
	{
		std::printf("\nMCP server\n");

		if (!std::filesystem::exists(executable))
		{
			std::printf("  skip  CyGPUInspectorMCP.exe not found\n");
			return;
		}

		// Permission levels are decided by the standalone, so they are checked here directly.
		Check(cygi::McpBridge::RequiredPermission("list_shaders") == cygi::McpPermission::read_only,
			"inspection is read only");
		Check(cygi::McpBridge::RequiredPermission("disable_shader") == cygi::McpPermission::debug_control,
			"disabling a shader needs Debug Control");
		Check(cygi::McpBridge::RequiredPermission("replace_shader") ==
			cygi::McpPermission::shader_modification, "replacing a shader needs Shader Modification");

		cygi::McpBridge bridge;
		Check(bridge.Permission() == cygi::McpPermission::read_only, "the default level is Read Only");
		Check(bridge.IsAllowed("get_frame_graph"), "reading is allowed by default");
		Check(!bridge.IsAllowed("disable_shader"), "acting is refused by default");
		Check(!bridge.IsAllowed("replace_shader"), "modifying is refused by default");
		bridge.SetPermission(cygi::McpPermission::debug_control);
		Check(bridge.IsAllowed("disable_shader"), "Debug Control allows disabling");
		Check(!bridge.IsAllowed("replace_shader"), "Debug Control still refuses modification");

		McpProcess mcp;
		if (!mcp.Start(executable))
		{
			Check(false, "the MCP server starts");
			return;
		}

		cygi::Json response;
		Check(mcp.Call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}", response),
			"initialize is answered");
		Check(response["id"].AsUInt() == 1, "the answer carries the request id");
		Check(!response["result"]["protocolVersion"].AsString().empty(), "a protocol version is announced");
		Check(response["result"]["serverInfo"]["name"].AsString() == "CyGPUInspectorMCP",
			"the server names itself");
		Check(response["result"]["capabilities"].Has("tools"), "tools are advertised as a capability");
		Check(!response["result"]["instructions"].AsString().empty(),
			"instructions tell an agent where to start");

		Check(mcp.Call("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", response),
			"tools/list is answered");

		const std::vector<cygi::Json> &tools = response["result"]["tools"].Items();
		Check(tools.size() >= 24, "every tool of the brief is exposed");

		bool has_graph = false;
		bool has_decompile = false;
		bool has_replace = false;
		bool has_timeline = false;
		bool has_command_state = false;
		bool has_deep_capture = false;
		bool all_described = true;
		for (const cygi::Json &tool : tools)
		{
			const std::string name = tool["name"].AsString();
			if (name == "get_frame_graph") has_graph = true;
			if (name == "get_shader_decompiled_hlsl") has_decompile = true;
			if (name == "replace_shader") has_replace = true;
			if (name == "get_frame_timeline") has_timeline = true;
			if (name == "get_command_state") has_command_state = true;
			if (name == "start_deep_capture") has_deep_capture = true;
			if (tool["description"].AsString().empty() || !tool["inputSchema"].IsObject())
				all_described = false;
		}
		Check(has_graph, "get_frame_graph is exposed");
		Check(has_decompile, "get_shader_decompiled_hlsl is exposed");
		Check(has_replace, "replace_shader is exposed");
		Check(has_timeline, "get_frame_timeline exposes the runtime capture");
		Check(has_command_state, "get_command_state exposes the deep capture");
		Check(has_deep_capture, "start_deep_capture can arm one");
		Check(all_described, "every tool has a description and an input schema");
		Check(cygi::McpBridge::RequiredPermission("get_frame_timeline") == cygi::McpPermission::read_only,
			"reading the timeline is read only");
		Check(cygi::McpBridge::RequiredPermission("start_deep_capture") ==
			cygi::McpPermission::debug_control,
			"arming a deep capture slows the game down, so it needs Debug Control");

		// With no standalone running, a call must fail clearly rather than hang or lie.
		Check(mcp.Call("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":"
			"{\"name\":\"get_session\",\"arguments\":{}}}", response), "tools/call is answered");
		Check(response["result"]["isError"].AsBool(), "a call without the standalone is an error");

		const std::string text = response["result"]["content"].Items().empty()
			? std::string() : response["result"]["content"].Items()[0]["text"].AsString();
		Check(text.find("CyGPUInspectorApp") != std::string::npos,
			"the error says the standalone is not running");

		Check(mcp.Call("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"nonsense\"}", response),
			"an unknown method is answered");
		Check(response.Has("error") && response["error"]["code"].AsNumber() == -32601,
			"an unknown method returns method not found");

		mcp.Stop();
	}
}

namespace
{
	// The C interface the Unreal plugin drives the add-on with, loaded the way the plugin loads it.
	// With no ReShade and no device behind it, it has to answer and refuse cleanly.
	void TestInProcessApi(const std::filesystem::path &directory)
	{
		std::printf("In-process interface of the add-on\n");
		const std::filesystem::path addon = directory / "CyGPUInspectorRS.addon64";
		HMODULE module = std::filesystem::exists(addon) ? LoadLibraryW(addon.c_str()) : nullptr;
		Check(module != nullptr, "the add-on loads as a plain DLL");
		if (module == nullptr)
			return;

		const auto version = reinterpret_cast<PFN_CyGPUInspectorRS_GetApiVersion>(
			GetProcAddress(module, CYGI_EXPORT_GET_API_VERSION));
		const auto status_of = reinterpret_cast<PFN_CyGPUInspectorRS_GetStatus>(
			GetProcAddress(module, CYGI_EXPORT_GET_STATUS));
		const auto request = reinterpret_cast<PFN_CyGPUInspectorRS_RequestCapture>(
			GetProcAddress(module, CYGI_EXPORT_REQUEST_CAPTURE));
		Check(version != nullptr && status_of != nullptr && request != nullptr, "the three functions are exported");
		if (version != nullptr && status_of != nullptr && request != nullptr)
		{
			Check(version() == CYGI_INPROCESS_API_VERSION, "the version is the one of the header");

			CygiStatus status = {};
			status.size = sizeof(status);
			status.device_count = 99;
			Check(status_of(&status) == 1 && status.device_count == 0 && status.api_version == CYGI_INPROCESS_API_VERSION,
				"with no device, the status says so");
			Check(status_of(nullptr) == 0, "a null status is refused");

			// An older caller that knows only the first fields gets those and nothing past them.
			struct { CygiStatus head; uint32_t guard; } short_status = {};
			short_status.head.size = offsetof(CygiStatus, tracking_level);
			short_status.head.tracking_level = 0xDEAD;
			short_status.guard = 0xBEEF;
			Check(status_of(&short_status.head) == 1 && short_status.head.tracking_level == 0xDEAD &&
			      short_status.guard == 0xBEEF, "a shorter status is filled no further than its size");

			CygiCaptureOptions options = {};
			options.size = sizeof(options);
			options.frame_count = 1;
			Check(request(&options) == 0, "a capture is refused while there is no device to capture");
			Check(request(nullptr) == 0, "a null request is refused");
		}
		FreeLibrary(module);
	}
}

int main(int argc, char **argv)
{
	std::filesystem::path fake_session = argc > 1
		? std::filesystem::path(argv[1])
		: std::filesystem::path(argv[0]).parent_path() / "CyGPUInspectorFakeSession.exe";

	if (!std::filesystem::exists(fake_session))
	{
		std::printf("CyGPUInspectorFakeSession.exe not found next to the test (%s)\n",
			fake_session.string().c_str());
		return 1;
	}

	std::wstring command_line = L"\"" + fake_session.wstring() + L"\"";
	STARTUPINFOW startup = { sizeof(startup) };
	PROCESS_INFORMATION process = {};
	if (CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
	                   nullptr, &startup, &process) == 0)
	{
		std::printf("could not start the fake session\n");
		return 1;
	}

	std::printf("fake session started (PID %lu)\n\n", process.dwProcessId);

	TestInProcessApi(std::filesystem::path(argv[0]).parent_path());

	{
		cygi::SessionDirectory directory;
		Check(directory.Open(), "the session directory opens");

		cygi::SessionEntry target;
		const bool found = WaitFor([&]() {
			for (const cygi::SessionEntry &entry : directory.List())
			{
				if (entry.process_id == process.dwProcessId)
				{
					target = entry;
					return true;
				}
			}
			return false;
		}, 5000);
		Check(found, "the fake session appears in the directory");

		if (found)
		{
			Check(target.process_name == "FakeGame.exe", "the process name is published");
			Check(target.api == cygi::GraphicsApi::d3d12, "the graphics API is published");
			Check(target.IsFresh(), "the heartbeat is fresh");

			cygi::SessionClient client;
			Check(client.Connect(target), "the client attaches to the ring");
			Check(client.IsControlConnected(), "the control pipe is connected");

			cygi::SessionModel &model = client.Model();

			const bool got_statics = WaitFor([&]() {
				std::lock_guard<std::mutex> lock(model.Mutex());
				return model.Shaders().size() >= 9 && model.Resources().size() >= 8 &&
				       model.Pipelines().size() >= 7;
			}, 5000);
			Check(got_statics, "shaders, pipelines and resources arrive");

			const bool got_frame = WaitFor([&]() {
				std::lock_guard<std::mutex> lock(model.Mutex());
				return model.LastFrame().events.size() > 2000;
			}, 5000);
			Check(got_frame, "a full frame of events arrives");

			{
				std::lock_guard<std::mutex> lock(model.Mutex());
				const cygi::FrameInfo &frame = model.LastFrame();

				Check(frame.draw_count == 2041, "the draw count matches what the producer sent");
				Check(frame.dispatch_count == 3, "the dispatch count matches");
				// 2041 draws + 3 dispatches + 3 bindings + 2 clears + 1 present.
				Check(frame.events.size() == static_cast<size_t>(frame.draw_count) + frame.dispatch_count + 6,
					"every event of the frame arrived");

				// Event indices must be contiguous: this is what proves the chunked transfer and
				// the ring wrap around did not lose or duplicate anything.
				bool contiguous = true;
				for (size_t i = 0; i < frame.events.size(); ++i)
					if (frame.events[i].index != static_cast<uint32_t>(i))
						contiguous = false;
				Check(contiguous, "event indices are contiguous");

				const cygi::ShaderInfo *gbuffer_ps = model.ShaderById(4);
				Check(gbuffer_ps != nullptr && gbuffer_ps->stage == cygi::ShaderStage::pixel,
					"shader 4 is a pixel shader");
				Check(gbuffer_ps != nullptr && gbuffer_ps->code.size() == gbuffer_ps->code_size,
					"the byte code arrived whole");
				Check(gbuffer_ps != nullptr && !gbuffer_ps->signature.IsZero(), "the signature arrived");
				Check(gbuffer_ps != nullptr && gbuffer_ps->draws_this_frame == 1200,
					"the gbuffer shader is credited with its 1200 draws");

				const cygi::ShaderInfo *lighting_cs = model.ShaderById(5);
				Check(lighting_cs != nullptr && lighting_cs->dispatches_this_frame == 1,
					"the lighting shader is credited with its dispatch");

				const cygi::ResourceInfo *scene_color = model.ResourceById(4);
				Check(scene_color != nullptr && scene_color->width == 2560 && scene_color->height == 1440,
					"SceneColor kept its dimensions");
				Check(scene_color != nullptr && scene_color->writes_this_frame == 1,
					"SceneColor is written once per frame");

				const cygi::ResourceInfo *back_buffer = model.ResourceById(7);
				Check(back_buffer != nullptr && (back_buffer->usage_flags & cygi::kUsageBackBuffer) != 0,
					"the back buffer is flagged");
				// Tonemap plus the 41 UI draws; binding it and presenting it are not writes.
				Check(back_buffer != nullptr && back_buffer->writes_this_frame == 41,
					"the back buffer is written by tonemap and the UI, and by nothing else");

				Check(model.EventsUsingShader(4).size() == 1200, "shader 4 correlates to 1200 events");
			}

			// ---- Frame graph: derived, since no game names its passes ------------------------
			{
				std::lock_guard<std::mutex> lock(model.Mutex());

				cygi::FrameGraph graph;
				graph.Build(model);
				Check(graph.IsValid(), "a frame graph is built");
				Check(graph.Passes().size() >= 6, "the frame is split into several passes");

				const cygi::GraphPass *depth_prepass = nullptr;
				const cygi::GraphPass *gbuffer = nullptr;
				const cygi::GraphPass *ui = nullptr;
				for (const cygi::GraphPass &pass : graph.Passes())
				{
					if (pass.name == "Depth Prepass") depth_prepass = &pass;
					if (pass.name == "GBuffer") gbuffer = &pass;
					if (pass.name == "UI") ui = &pass;
				}

				Check(depth_prepass != nullptr, "the depth prepass is recognised");
				Check(depth_prepass != nullptr && depth_prepass->draw_count == 800,
					"the depth prepass holds its 800 draws");
				Check(depth_prepass != nullptr && depth_prepass->render_targets.empty() &&
					depth_prepass->depth_target == 1, "the depth prepass writes depth and no colour");

				Check(gbuffer != nullptr, "the GBuffer pass is recognised");
				Check(gbuffer != nullptr && gbuffer->render_targets.size() == 3,
					"the GBuffer pass sees its three render targets");
				Check(gbuffer != nullptr && gbuffer->draw_count == 1200, "the GBuffer holds its 1200 draws");
				Check(gbuffer != nullptr && gbuffer->depth_target == 1, "the GBuffer reuses the depth buffer");

				Check(ui != nullptr, "the UI pass is recognised");
				Check(ui != nullptr && ui->draw_count == 40, "the UI pass holds its 40 draws");

				// The dependency that matters: lighting reads the depth the prepass produced.
				bool depth_edge = false;
				for (const cygi::GraphEdge &edge : graph.Edges())
					if (edge.resource_id == 1 && depth_prepass != nullptr && edge.from_pass == depth_prepass->id)
						depth_edge = true;
				Check(depth_edge, "a dependency is derived from the depth buffer");

				const cygi::ResourceFlow *scene_color_flow = graph.FlowOf(4);
				Check(scene_color_flow != nullptr && !scene_color_flow->written_by.empty(),
					"SceneColor has a producer");
				Check(scene_color_flow != nullptr && !scene_color_flow->read_by.empty(),
					"SceneColor has a consumer");

				const cygi::GraphPass *owner = graph.PassOfEvent(depth_prepass != nullptr
					? depth_prepass->first_event + 10 : 0);
				Check(owner != nullptr && depth_prepass != nullptr && owner->id == depth_prepass->id,
					"an event maps back to its pass");

				// Every name must carry where it came from; nothing is presented as a fact.
				bool all_attributed = true;
				for (const cygi::GraphPass &pass : graph.Passes())
					if (pass.origin == cygi::PassNameOrigin::heuristic && pass.confidence <= 0.0f)
						all_attributed = false;
				Check(all_attributed, "every derived name carries a confidence");

				// The same frame on a time axis. Without timings it is laid out by command count,
				// which must still tile the frame: the view is never empty and never overlaps.
				cygi::FrameTimeline untimed;
				untimed.Build(cygi::FrameTimings(), graph, model.LastFrame().events);
				Check(!untimed.Timed() && untimed.Spans().size() == graph.Passes().size(),
					"without timings, every pass still has a place on the timeline");
				Check(untimed.Length() == static_cast<double>(graph.Passes().back().last_event + 1),
					"and the axis is in commands");
				bool tiles = true;
				for (size_t i = 1; i < untimed.Spans().size(); ++i)
					if (untimed.Spans()[i].start + 1e-9 < untimed.Spans()[i - 1].end)
						tiles = false;
				Check(tiles, "passes follow each other without overlapping");
			}

			// Control path: disable a shader and watch the producer mark its draws as skipped.
			Check(client.SendShaderCommand(cygi::ShaderCommand::disable, 4), "the disable command is acknowledged");

			const bool skipped = WaitFor([&]() {
				std::lock_guard<std::mutex> lock(model.Mutex());
				for (const cygi::FrameEvent &event : model.LastFrame().events)
					if (event.pipeline_id == 2 && (event.flags & cygi::kEventSkipped) != 0)
						return true;
				return false;
			}, 5000);
			Check(skipped, "the draws of the disabled shader come back marked as skipped");

			Check(client.SendShaderCommand(cygi::ShaderCommand::enable, 4), "the enable command is acknowledged");
			Check(client.SetLevel(cygi::TrackingLevel::idle), "the tracking level can be changed");


			// ---- Runtime replacement (milestone 7) --------------------------------------------
			// The level check above left the producer idle, so frames have to flow again before
			// anything can be observed coming back.
			Check(client.SetLevel(cygi::TrackingLevel::tracking), "tracking is resumed");
			{
				// Any byte code will do here: the fake producer checks the framing, not the shader.
				std::vector<uint8_t> replacement(256, 0x42);
				std::string message;
				Check(client.ReplaceShader(4, replacement, message),
					"the replacement byte code is accepted");
				std::printf("        %s\n", message.c_str());
			}

			const bool replaced = WaitFor([&]() {
				std::lock_guard<std::mutex> lock(model.Mutex());
				for (const cygi::FrameEvent &event : model.LastFrame().events)
					if (event.pipeline_id == 2 && (event.flags & cygi::kEventReplaced) != 0)
						return true;
				return false;
			}, 5000);
			Check(replaced, "the draws of the replaced shader come back marked as replaced");

			{
				std::lock_guard<std::mutex> lock(model.Mutex());
				const cygi::ShaderInfo *shader = model.ShaderById(4);
				Check(shader != nullptr && shader->replaced, "the model remembers the replacement");
			}

			// ---- GPU timings (milestone 8) ----------------------------------------------------
			const bool timed = WaitFor([&]() {
				std::lock_guard<std::mutex> lock(model.Mutex());
				return model.Timings().IsValid();
			}, 5000);
			Check(timed, "GPU timings arrive");

			if (timed)
			{
				std::lock_guard<std::mutex> lock(model.Mutex());
				const cygi::FrameTimings &timings = model.Timings();

				Check(timings.frequency == 10000000, "the timestamp frequency comes from the session");
				Check(timings.total_ticks > 0, "the timings add up to something");
				Check(!timings.by_event.empty(), "timings are indexed by event");

				// 10 MHz: 1000 ticks is exactly 0.1 ms. The conversion must not drift.
				const double tenth = timings.Milliseconds(1000);
				Check(tenth > 0.0999 && tenth < 0.1001, "ticks convert to milliseconds correctly");

				Check(timings.frame_index < model.LastFrame().index,
					"the timings carry the older frame they measured, not the current one");

				uint64_t attributed = 0;
				for (const cygi::ShaderInfo &shader : model.Shaders())
					attributed += shader.gpu_ticks;
				Check(attributed > 0, "time is attributed to shaders through their pipelines");

				// The timing view places passes on a time axis, which needs where each measurement
				// starts, not only how long it lasts.
				Check(timings.HasStarts(), "timings carry where they start on the GPU");
				std::vector<std::pair<uint32_t, uint64_t>> starts(timings.start_by_event.begin(),
					timings.start_by_event.end());
				std::sort(starts.begin(), starts.end());
				bool ordered = !starts.empty();
				for (size_t i = 1; i < starts.size(); ++i)
					if (starts[i].second < starts[i - 1].second)
						ordered = false;
				Check(ordered, "on one queue, starts follow the order the commands ran in");

				// The timeline built from those timings is what the view, the pass list and the
				// selected pass all read: one length per pass, never two.
				cygi::FrameGraph timed_graph;
				timed_graph.Build(model);
				cygi::FrameTimeline timeline;
				timeline.Build(timings, timed_graph, model.LastFrame().events);
				Check(timeline.Timed() && timeline.MeasuredPositions(),
					"the timeline uses the measured positions");
				double total = 0.0;
				bool ordered_spans = true;
				bool any_measured = false;
				for (size_t i = 0; i < timeline.Spans().size(); ++i)
				{
					const cygi::FrameTimeline::Span &span = timeline.Spans()[i];
					total += span.Length();
					any_measured = any_measured || (span.measured && span.Length() > 0.0);
					if (i > 0 && span.start + 1e-9 < timeline.Spans()[i - 1].end)
						ordered_spans = false;
				}
				Check(any_measured, "measured passes have a length");
				Check(ordered_spans, "passes sit one after the other on the GPU axis");
				Check(total <= timeline.Length() + 1e-6, "the passes never add up to more than the frame");
				const cygi::GraphPass *first_pass = timed_graph.PassById(1);
				Check(first_pass != nullptr &&
					timeline.ShareOf(first_pass->id) >= 0.0 && timeline.ShareOf(first_pass->id) <= 1.0,
					"a pass's share of the frame is a fraction of it");
			}

			// An add-on built before start times existed sends the sixteen byte layout and a zero
			// where the element size now is. It must still be read, and read as having no starts,
			// rather than as elements of the wrong size.
			{
				cygi::SessionModel old_model;
				struct
				{
					cygi::TimingResultsRecord record;
					uint8_t elements[2 * cygi::kTimingResultSizeV1];
				} payload = {};
				payload.record.frame_index = 7;
				payload.record.count = 2;
				payload.record.result_size = 0;
				const uint32_t first[4] = { 10, 1, 500, 0 };
				const uint32_t second[4] = { 20, 1, 700, 0 };
				std::memcpy(payload.elements, first, sizeof(first));
				std::memcpy(payload.elements + cygi::kTimingResultSizeV1, second, sizeof(second));

				cygi::RecordHeader header = {};
				header.type = cygi::RecordType::timing_results;
				header.size = static_cast<uint32_t>(sizeof(header) + sizeof(payload));
				old_model.ApplyRecord(header, reinterpret_cast<const uint8_t *>(&payload),
					static_cast<uint32_t>(sizeof(payload)));

				std::lock_guard<std::mutex> lock(old_model.Mutex());
				const cygi::FrameTimings &old_timings = old_model.Timings();
				Check(old_timings.by_event.size() == 2 && old_timings.total_ticks == 1200,
					"timings from an older add-on are still read, element by element");
				Check(!old_timings.HasStarts(), "and are known to carry no start times");
			}

			// ---- Continuous timeline --------------------------------------------------------
			// Frame after frame on the GPU clock, each cut into passes with its own events. The
			// fake producer runs one frame every 16.6 ms and keeps the GPU busy for about 1.5 ms
			// of it, so what is between two frames is a wait of about 15 ms.
			{
				{
					std::lock_guard<std::mutex> lock(model.Mutex());
					Check(model.Timings().HasSpan(), "timings say where their frame sits on the GPU clock");
					model.TakeQueuedTimings();   // older ones whose frames may be gone already
				}

				cygi::FrameGraph fallback;
				{
					std::lock_guard<std::mutex> lock(model.Mutex());
					fallback.Build(model);
				}

				cygi::FrameTrack track;
				const bool enough = WaitFor([&]() {
					std::lock_guard<std::mutex> lock(model.Mutex());
					for (const cygi::FrameTimings &queued : model.TakeQueuedTimings())
						track.Add(queued, model.FrameByIndex(queued.frame_index), model, fallback);
					return track.Frames().size() >= 5;
				}, 5000);
				Check(enough, "measured frames accumulate on the continuous track");
				Check(track.MeasuredAxis(), "frames are placed with the GPU clock the add-on sends");

				bool one_after_another = true;
				bool passes_inside = true;
				bool waits_measured = true;
				bool own_structure = true;
				bool have_passes = true;
				const std::deque<cygi::TrackFrame> &frames = track.Frames();
				for (size_t i = 0; i < frames.size(); ++i)
				{
					const cygi::TrackFrame &frame = frames[i];
					if (frame.Busy() <= 0.0 || (i > 0 && frame.begin < frames[i - 1].end))
						one_after_another = false;
					for (const cygi::TrackPass &pass : frame.passes)
						if (pass.start < frame.begin - 1e-6 || pass.end > frame.end + 1e-6)
							passes_inside = false;
					if (i > 0 && frames[i - 1].index + 1 == frame.index)
					{
						const double wait = frame.begin - frames[i - 1].end;
						if (!(wait > 10.0 && wait < 16.7))
							waits_measured = false;
					}
					// The first frames can predate the producer resuming after the idle level the
					// test set earlier: their events never existed, and they are rightly flagged as
					// cut with another frame's. From the third on, every frame has its own.
					if (i >= 2)
						own_structure = own_structure && frame.own_structure;
					have_passes = have_passes && !frame.passes.empty();
				}
				Check(one_after_another, "frames follow one another without overlapping");
				Check(passes_inside, "every pass sits inside the frame it belongs to");
				Check(waits_measured, "the wait between two frames is the rest of the frame period");
				Check(own_structure, "in steady state, each frame is cut into passes with its own commands");
				Check(have_passes, "every frame kept has its passes");
				Check(frames.size() > 2 && track.FrameByIndex(frames[2].index) == &frames[2],
					"a frame picked by its number is found on the track");
			}

			Check(client.SendShaderCommand(cygi::ShaderCommand::restore, 4),
				"restoring the shader is acknowledged");

			// ---- Deep capture -----------------------------------------------------------------
			// The expensive one shot: armed over the control pipe, it records state per command
			// for a few frames and then disarms itself. What is checked here is the whole path —
			// arming, the per command records, and the fact that it stops on its own.
			{
				std::string message;
				Check(client.StartDeepCapture(2, true, true, false, false, message),
					"a deep capture is armed over the control pipe");
				if (!message.empty())
					std::printf("        %s\n", message.c_str());

				const bool arrived = WaitFor([&]() {
					std::lock_guard<std::mutex> lock(model.Mutex());
					return model.LastFrame().HasDeepCapture();
				}, 5000);
				Check(arrived, "per command state arrives for the captured frame");

				{
					std::lock_guard<std::mutex> lock(model.Mutex());
					const cygi::ResourceInfo *depth = model.ResourceById(1);
					Check(depth != nullptr && depth->name == "SceneDepth",
						"a deep capture brings the names the game gave its resources");
					Check(depth != nullptr && depth->Describe().find("\"SceneDepth\"") != std::string::npos,
						"and the name is part of how a resource is described");
				}

				{
					std::lock_guard<std::mutex> lock(model.Mutex());
					const cygi::FrameInfo &frame = model.LastFrame();

					Check(!frame.draw_states.empty(), "commands carry their recorded state");
					Check(!frame.barriers.empty(), "barriers are recorded");

					// The list is kept sorted so the interface can binary search it.
					bool sorted = true;
					for (size_t i = 1; i < frame.draw_states.size(); ++i)
						if (frame.draw_states[i - 1].record.event_index >=
						    frame.draw_states[i].record.event_index)
							sorted = false;
					Check(sorted, "recorded commands are kept in event order");

					// Every recorded state must point at a command that really exists in the frame,
					// which is what proves the event indices were remapped correctly.
					bool all_resolve = true;
					bool any_binding = false;
					for (const cygi::CommandState &state : frame.draw_states)
					{
						if (state.record.event_index >= frame.events.size())
							all_resolve = false;
						if (state.bindings.size() != state.record.binding_count)
							all_resolve = false;
						if (!state.bindings.empty())
							any_binding = true;
					}
					Check(all_resolve, "every recorded state points at a real command of the frame");
					Check(any_binding, "descriptors were recorded");

					const cygi::CommandState *first = frame.draw_states.empty()
						? nullptr : &frame.draw_states.front();
					Check(first != nullptr &&
						frame.DrawStateOfEvent(first->record.event_index) == first,
						"a command's state can be looked up by its event index");
					Check(frame.DrawStateOfEvent(0xFFFFFFFFu) == nullptr,
						"looking up a command that was not recorded returns nothing");

					// The constant buffer binding the producer sent, read back as a b0 slot.
					bool found_cbv = false;
					bool found_target = false;
					if (first != nullptr)
					{
						for (const cygi::DrawBinding &binding : first->bindings)
						{
							if (binding.kind == static_cast<uint8_t>(cygi::SlotKind::constant_buffer) &&
							    binding.slot == 0 && binding.size == 256)
								found_cbv = true;
							if (binding.kind == static_cast<uint8_t>(cygi::SlotKind::render_target) ||
							    binding.kind == static_cast<uint8_t>(cygi::SlotKind::unordered_access))
								found_target = true;
						}
					}
					Check(found_cbv, "a constant buffer binding keeps its slot and its size");
					Check(found_target, "the command's output is in the binding list too");

					Check(model.PipelineStateById(2) != nullptr,
						"the fixed function state of a pipeline arrives");
					const cygi::PipelineStateRecord *state = model.PipelineStateById(2);
					Check(state != nullptr && state->depth_enable != 0,
						"and it says whether the depth test was on");
					Check(state != nullptr && state->known_fields != 0,
						"and which of its fields the graphics API actually reported");
				}

				// It has to stop by itself: a capture that forgets to disarm would leave the game
				// running the expensive path forever.
				const bool finished = WaitFor([&]() {
					std::lock_guard<std::mutex> lock(model.Mutex());
					return model.CaptureState().stage == cygi::CaptureStage::finished;
				}, 5000);
				Check(finished, "the deep capture disarms itself when its frames are done");

				{
					std::lock_guard<std::mutex> lock(model.Mutex());
					Check(!model.DeepCaptureRunning(), "and reports that it is no longer running");
					Check(model.CaptureState().frames_done == 2, "it ran for exactly the frames asked for");
					Check(model.CaptureState().mode == cygi::CaptureMode::deep, "it names itself a deep capture");
				}
			}

			// ---- Offline capture (milestone 11) ----------------------------------------------
			{
				const std::filesystem::path directory =
					std::filesystem::temp_directory_path() / "CyGPUInspectorCaptureTest";
				std::error_code code;
				std::filesystem::remove_all(directory, code);

				// A deep capture is armed first so the saved capture carries per command state,
				// then tracking is stopped so the frame on screen stops moving underneath us:
				// otherwise the next frame, which has no state, would be the one saved.
				// Sixteen frames, not one: tracking is stopped from another thread, and the frame
				// on screen has to still be a captured one when it lands. One frame would be a race.
				std::string arm_message;
				Check(client.StartDeepCapture(16, true, true, false, false, arm_message),
					"a deep capture is armed before saving");
				const bool deep_ready = WaitFor([&]() {
					std::lock_guard<std::mutex> lock(model.Mutex());
					return model.LastFrame().HasDeepCapture();
				}, 5000);
				Check(deep_ready, "its state arrives before the capture is written");
				Check(client.SetLevel(cygi::TrackingLevel::idle), "tracking is stopped to freeze the frame");

				cygi::FrameGraph graph;
				uint32_t saved_draws = 0;
				size_t saved_events = 0;
				size_t saved_shaders = 0;
				size_t saved_draw_states = 0;
				size_t saved_barriers = 0;
				std::string save_error;
				bool saved = false;
				{
					std::lock_guard<std::mutex> lock(model.Mutex());
					graph.Build(model);
					saved_draws = model.LastFrame().draw_count;
					saved_events = model.LastFrame().events.size();
					saved_shaders = model.Shaders().size();
					saved_draw_states = model.LastFrame().draw_states.size();
					saved_barriers = model.LastFrame().barriers.size();
					saved = cygi::CaptureArchive::Save(model, graph, directory, save_error);
				}
				Check(saved, "a capture is written");
				if (!saved)
					std::printf("        %s\n", save_error.c_str());

				Check(std::filesystem::exists(directory / "capture.json"), "capture.json is there");
				Check(std::filesystem::exists(directory / "events.bin"), "events.bin is there");
				Check(std::filesystem::exists(directory / "drawstates.bin"),
					"the deep capture state is written beside them");
				Check(std::filesystem::exists(directory / "barriers.bin"), "and so are the barriers");

				cygi::CaptureInfo info;
				Check(cygi::CaptureArchive::ReadInfo(directory, info), "the header reads back");
				Check(info.draw_count == saved_draws, "the header keeps the draw count");
				Check(info.process_name == "FakeGame.exe", "the header names the process");
				Check(info.pass_count >= 6, "the derived passes travel with the capture");

				// Reload into a fresh model: this is the whole point of a capture.
				cygi::SessionModel reopened;
				cygi::CaptureInfo loaded;
				std::string load_error;
				const bool opened = cygi::CaptureArchive::Load(directory, reopened, loaded, load_error);
				Check(opened, "the capture reopens");
                if (!opened)
					std::printf("        %s\n", load_error.c_str());

				if (opened)
				{
					std::lock_guard<std::mutex> lock(reopened.Mutex());
					Check(reopened.HasSession(), "the session survives the round trip");
					Check(reopened.LastFrame().draw_count == saved_draws,
						"the draw count survives the round trip");
					Check(reopened.LastFrame().events.size() == saved_events,
						"every event survives the round trip");
					Check(reopened.Shaders().size() == saved_shaders,
						"the shader table survives the round trip");

					const cygi::ResourceInfo *named = reopened.ResourceById(1);
					Check(named != nullptr && named->name == "SceneDepth", "resource names survive the round trip");

					const cygi::ShaderInfo *shader = reopened.ShaderById(4);
					Check(shader != nullptr && !shader->signature.IsZero(),
						"a shader keeps its signature");
					Check(shader != nullptr && shader->code.size() == shader->code_size &&
						shader->code_size != 0, "the byte code travels with the capture");
					Check(shader != nullptr && shader->draws_this_frame == 1200,
						"per shader statistics are recomputed from the reloaded events");

					Check(reopened.LastFrame().draw_states.size() == saved_draw_states &&
						saved_draw_states != 0,
						"the per command state survives the round trip");
					Check(reopened.LastFrame().barriers.size() == saved_barriers && saved_barriers != 0,
						"and so do the barriers");

					// A binding is only useful if it still says which slot it was on and what was
					// bound there, so the values are compared rather than just the count.
					const cygi::CommandState *reloaded = reopened.LastFrame().draw_states.empty()
						? nullptr : &reopened.LastFrame().draw_states.front();
					Check(reloaded != nullptr && !reloaded->bindings.empty(),
						"a reopened command still lists its descriptors");
					bool binding_intact = false;
					if (reloaded != nullptr)
						for (const cygi::DrawBinding &binding : reloaded->bindings)
							if (binding.kind == static_cast<uint8_t>(cygi::SlotKind::constant_buffer) &&
							    binding.slot == 0 && binding.size == 256)
								binding_intact = true;
					Check(binding_intact, "with the same slot, the same kind and the same range");

					// The graph must be rebuildable from the reopened model, not just from the file.
					cygi::FrameGraph reopened_graph;
					reopened_graph.Build(reopened);
					Check(reopened_graph.Passes().size() == graph.Passes().size(),
						"the same passes are derived from the reopened capture");
				}

				// Comparing a capture with itself must show no difference at all.
				cygi::SessionModel other;
				cygi::CaptureInfo other_info;
				std::string other_error;
				if (cygi::CaptureArchive::Load(directory, other, other_info, other_error))
				{
					const cygi::CaptureDiff diff = cygi::CompareCaptures(reopened, other);
					Check(diff.draw_delta == 0 && diff.shader_delta == 0 && diff.resource_delta == 0,
						"a capture compared with itself shows no difference");
					Check(diff.only_in_a.empty() && diff.only_in_b.empty(),
						"no shader is reported as exclusive to one side");
				}

				std::filesystem::remove_all(directory, code);
			}

			Check(client.SetLevel(cygi::TrackingLevel::tracking), "tracking is resumed after the save");

			// ---- GPU sharing: the whole point of milestone 3 ----------------------------------
			Check(client.RequestPreview(2), "the preview request is acknowledged");

			cygi::PreviewReadyRecord preview = {};
			const bool got_preview = WaitFor([&]() {
				std::lock_guard<std::mutex> lock(model.Mutex());
				return model.TakePreviewUpdate(preview);
			}, 5000);
			Check(got_preview, "the producer announces a shared texture");

			if (got_preview && preview.status == cygi::PreviewStatus::ready)
			{
				ID3D11Device *device = nullptr;
				ID3D11DeviceContext *context = nullptr;
				const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
				HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
					D3D11_SDK_VERSION, &device, nullptr, &context);
				if (FAILED(hr))
					hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1,
						D3D11_SDK_VERSION, &device, nullptr, &context);
				Check(SUCCEEDED(hr), "the test process can create its own D3D11 device");

				if (SUCCEEDED(hr))
				{
					cygi::SharedTexture shared;
					const bool opened = shared.Open(device, preview.shared_handle, preview.is_nt_handle != 0,
						preview.format, preview.width, preview.height);
					Check(opened, "the texture of the other process opens in ours");
					if (!opened)
						std::printf("        %s\n", shared.LastError().c_str());

					Check(opened && shared.Width() == 256 && shared.Height() == 256,
						"the shared texture keeps its dimensions");
					Check(opened && shared.View() != nullptr, "a shader resource view can be created");

					// Read the pixels back to prove this really is the producer's memory. This is
					// the only CPU readback in the whole system, and it exists only for this test.
					if (opened)
					{
						D3D11_TEXTURE2D_DESC staging_desc = {};
						shared.Texture()->GetDesc(&staging_desc);
						staging_desc.Usage = D3D11_USAGE_STAGING;
						staging_desc.BindFlags = 0;
						staging_desc.MiscFlags = 0;
						staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

						ID3D11Texture2D *staging = nullptr;
						if (SUCCEEDED(device->CreateTexture2D(&staging_desc, nullptr, &staging)))
						{
							context->CopyResource(staging, shared.Texture());

							D3D11_MAPPED_SUBRESOURCE mapped = {};
							if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
							{
								const uint8_t *rows = static_cast<const uint8_t *>(mapped.pData);
								const uint32_t pixel = *reinterpret_cast<const uint32_t *>(rows + 20 * mapped.RowPitch + 10 * 4);
								// The producer writes R = x, G = y, B = 0x80, A = 0xFF.
								const uint32_t expected = 0xFF000000u | (0x80u << 16) | (20u << 8) | 10u;
								Check(pixel == expected, "the pixels are the ones the producer wrote");
								if (pixel != expected)
									std::printf("        expected 0x%08X, read 0x%08X\n", expected, pixel);
								context->Unmap(staging, 0);
							}
							staging->Release();
						}
					}
				}

				if (context != nullptr) context->Release();
				if (device != nullptr) device->Release();
			}
			else if (got_preview)
			{
				std::printf("        preview unavailable: %s\n", cygi::PreviewStatusName(preview.status));
			}

			Check(client.StopPreview(), "the preview can be stopped");

			client.Disconnect();
			Check(!client.IsAttached(), "the client detaches cleanly");
		}

	}

	TestMcpServer(std::filesystem::path(argv[0]).parent_path() / "CyGPUInspectorMCP.exe");

	TerminateProcess(process.hProcess, 0);
	WaitForSingleObject(process.hProcess, 2000);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);

	std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
