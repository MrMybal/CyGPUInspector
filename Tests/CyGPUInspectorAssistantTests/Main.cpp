// CyGPUInspectorAssistantTests — the AI assistant and the HLSL editor's parsing, with no GPU, no
// game and no real provider.
//
// The provider is either a scripted fake, or a small HTTP server on the loopback that plays an
// OpenAI-compatible API, so the WinHTTP client, the JSON contract, the chat loop, the tool queue
// and the approvals are all exercised the way the application runs them. Nothing here sends
// anything outside this machine.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Assistant/Assistant.hpp"
#include "Assistant/AssistantAgent.hpp"
#include "Assistant/AssistantProvider.hpp"
#include "Editor/CompilerMessages.hpp"

#include <CyGPUInspectorCore/Json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace cygi;
using namespace cygi::assistant;

namespace
{
	int g_checks = 0;
	int g_failures = 0;

	void Check(bool condition, const char *what)
	{
		++g_checks;
		if (!condition)
		{
			++g_failures;
			std::printf("  FAIL  %s\n", what);
		}
	}

	void CheckEqual(const std::string &actual, const std::string &expected, const char *what)
	{
		++g_checks;
		if (actual != expected)
		{
			++g_failures;
			std::printf("  FAIL  %s\n        expected %s\n        actual   %s\n", what, expected.c_str(), actual.c_str());
		}
	}

	Json Parse(const std::string &text)
	{
		Json value;
		Json::Parse(text, value);
		return value;
	}

	// A provider that answers from a script and remembers what it was sent.
	class FakeProvider final : public Provider
	{
	public:
		std::deque<Json> replies;
		std::vector<std::string> contents;
		std::vector<std::string> systems;

		Text Check(Cancellation &) override { return "fake"; }

		Json Complete(const std::string &system, const std::string &content, Cancellation &) override
		{
			systems.push_back(system);
			contents.push_back(content);
			if (replies.empty())
				throw Error("script exhausted");
			Json reply = replies.front();
			replies.pop_front();
			return reply;
		}

		std::string Describe() const override { return "fake-model"; }
	};

	Json Step(const std::string &message, const std::string &action, const std::string &arguments = "{}")
	{
		Json step = Json::Object();
		step["message"] = Json(message);
		step["action"] = Json(action);
		step["arguments"] = Parse(arguments);
		return step;
	}

	// ---- a loopback HTTP server playing a Chat Completions API -----------------------------------

	class FakeApi
	{
	public:
		struct Reply
		{
			int status = 200;
			std::string body;
			std::string headers;
			int delay_ms = 0;
		};

		struct Request
		{
			std::string head;
			std::string body;
		};

		bool Start()
		{
			WSADATA data;
			if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
				return false;
			m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			sockaddr_in address = {};
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			address.sin_port = 0;
			if (bind(m_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(m_listener, 8) != 0)
				return false;
			int length = sizeof(address);
			getsockname(m_listener, reinterpret_cast<sockaddr *>(&address), &length);
			m_port = ntohs(address.sin_port);
			m_thread = std::thread([this]() { Serve(); });
			return true;
		}

		void Stop()
		{
			m_stop = true;
			closesocket(m_listener);
			if (m_thread.joinable())
				m_thread.join();
			WSACleanup();
		}

		std::string Url() const { return "http://127.0.0.1:" + std::to_string(m_port) + "/v1/chat/completions"; }

		void Push(Reply reply)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_replies.push_back(std::move(reply));
		}

		void PushCompletion(const Json &content, const char *finish = "stop")
		{
			Json message = Json::Object();
			message["role"] = Json("assistant");
			message["content"] = Json(content.Write(-1));
			Json choice = Json::Object();
			choice["index"] = Json(0);
			choice["finish_reason"] = Json(finish);
			choice["message"] = message;
			Json choices = Json::Array();
			choices.Push(choice);
			Json body = Json::Object();
			body["choices"] = choices;
			Reply reply;
			reply.body = body.Write(-1);
			Push(reply);
		}

		std::vector<Request> Requests()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_requests;
		}

	private:
		void Serve()
		{
			while (!m_stop)
			{
				SOCKET client = accept(m_listener, nullptr, nullptr);
				if (client == INVALID_SOCKET)
					break;

				std::string data;
				char buffer[65536];
				size_t header_end = std::string::npos;
				size_t content_length = 0;
				for (;;)
				{
					const int read = recv(client, buffer, sizeof(buffer), 0);
					if (read <= 0)
						break;
					data.append(buffer, static_cast<size_t>(read));
					if (header_end == std::string::npos)
					{
						header_end = data.find("\r\n\r\n");
						if (header_end != std::string::npos)
						{
							std::string lower = data.substr(0, header_end);
							for (char &c : lower)
								c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
							const size_t at = lower.find("content-length:");
							if (at != std::string::npos)
								content_length = static_cast<size_t>(std::strtoul(lower.c_str() + at + 15, nullptr, 10));
						}
					}
					if (header_end != std::string::npos && data.size() >= header_end + 4 + content_length)
						break;
				}

				Reply reply;
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					if (header_end != std::string::npos)
						m_requests.push_back({ data.substr(0, header_end), data.substr(header_end + 4) });
					if (!m_replies.empty())
					{
						reply = m_replies.front();
						m_replies.pop_front();
					}
					else
					{
						reply.status = 500;
						reply.body = "{}";
					}
				}
				if (reply.delay_ms > 0)
					std::this_thread::sleep_for(std::chrono::milliseconds(reply.delay_ms));

				const std::string response = "HTTP/1.1 " + std::to_string(reply.status) + " X\r\nContent-Type: application/json\r\n" +
					reply.headers + "Content-Length: " + std::to_string(reply.body.size()) + "\r\nConnection: close\r\n\r\n" + reply.body;
				send(client, response.data(), static_cast<int>(response.size()), 0);
				shutdown(client, SD_SEND);
				closesocket(client);
			}
		}

		SOCKET m_listener = INVALID_SOCKET;
		uint16_t m_port = 0;
		std::thread m_thread;
		std::atomic<bool> m_stop{ false };
		std::mutex m_mutex;
		std::deque<Reply> m_replies;
		std::vector<Request> m_requests;
	};

	// ---- tests ------------------------------------------------------------------------------------

	void TestText()
	{
		std::printf("Messages\n");
		CheckEqual(English(Text::Format("a %s b %s", { Text::Literal("x"), Text("y") })), "a x b y", "values fill the placeholders in order");
		CheckEqual(English(Text::Format("%s and %s", { Text::Literal("one") })), "one and ", "a missing value leaves nothing, never garbage");
		CheckEqual(English(Text::Literal("100% %s")), "100% %s", "literal text is never formatted");
		CheckEqual(English(Text::Format("x %s", { Text::Format("(%s)", { Text::Literal("inner") }) })), "x (inner)", "messages nest");
	}

	void TestJsonReplies()
	{
		std::printf("Model replies\n");
		Json out;
		Check(ParseJsonReply("{\"a\":1}", out) && out["a"].AsNumber() == 1.0, "a bare object");
		Check(ParseJsonReply("  ```json\n{\"a\":2}\n```  ", out) && out["a"].AsNumber() == 2.0, "an object in a fence");
		Check(ParseJsonReply("Here it is: {\"a\":3} hope it helps", out) && out["a"].AsNumber() == 3.0, "an object inside a sentence");
		Check(!ParseJsonReply("no json here", out), "text without an object is refused");
		Check(!ParseJsonReply("[1,2]", out), "an array is not a reply");

		Json emoji;
		Check(Json::Parse("\"\\ud83d\\ude00\"", emoji) && emoji.AsString() == "\xF0\x9F\x98\x80",
			"an escaped emoji becomes one 4-byte UTF-8 character");
		Check(Json::Parse("\"\\u00e9\"", emoji) && emoji.AsString() == "\xC3\xA9", "and Latin-1 still works");
	}

	void TestEndpoints()
	{
		std::printf("Endpoints\n");
		auto accepted = [](const char *url) {
			try { ValidateEndpoint(url); return true; }
			catch (const Error &) { return false; }
		};
		Check(accepted("https://api.example.com/v1/chat/completions"), "HTTPS is accepted");
		Check(accepted("http://localhost:11434/v1/chat/completions"), "HTTP on localhost is accepted");
		Check(accepted("http://127.0.0.1:1234/v1/chat/completions"), "HTTP on 127.0.0.1 is accepted");
		Check(!accepted("http://example.com/v1/chat/completions"), "HTTP to another machine is refused");
		Check(!accepted("https://user:secret@example.com/v1/chat/completions"), "credentials in the URL are refused");
		Check(!accepted(""), "an empty URL is refused");
		Check(!accepted("not a url"), "garbage is refused");
	}

	void TestQuoting()
	{
		std::printf("Command lines\n");
		const std::vector<std::wstring> arguments = {
			L"", L"simple", L"with space", L"quote\"inside", L"trailing\\", L"back\\\\slash \"q\" end\\",
			L"{\"mcpServers\":{}}", L"line one\nline two", L"C:\\Program Files\\x\\",
		};
		std::wstring command = L"program.exe";
		for (const std::wstring &argument : arguments)
			command += L" " + QuoteArgument(argument);

		int count = 0;
		LPWSTR *parsed = CommandLineToArgvW(command.c_str(), &count);
		bool same = parsed != nullptr && count == static_cast<int>(arguments.size()) + 1;
		for (size_t i = 0; same && i < arguments.size(); ++i)
			same = arguments[i] == parsed[i + 1];
		LocalFree(parsed);
		Check(same, "every argument comes back unchanged, empty ones, quotes and trailing backslashes included");
	}

	void TestCompilerMessages()
	{
		std::printf("Compiler messages\n");
		const std::vector<CompilerMessage> fxc = ParseCompilerMessages(
			"C:\\work\\edited.hlsl(12,5-9): error X3004: undeclared identifier 'foo'\r\n"
			"edited.hlsl(3,1): warning X3206: implicit truncation of vector type\r\n"
			"compilation failed; no code produced\r\n");
		Check(fxc.size() == 2, "FXC: two messages, the summary line is not one");
		Check(fxc.size() == 2 && fxc[0].line == 12 && fxc[0].column == 5 && fxc[0].error, "FXC: line, column and error");
		Check(fxc.size() == 2 && fxc[1].line == 3 && !fxc[1].error, "FXC: a warning is a warning");

		const std::vector<CompilerMessage> dxc = ParseCompilerMessages(
			"edited.hlsl:7:12: error: use of undeclared identifier 'bar'\n"
			"    float x = bar;\n"
			"              ^\n"
			"C:\\dir\\edited.hlsl:4:2: warning: implicit truncation\n");
		Check(dxc.size() == 2, "DXC: two messages");
		Check(dxc.size() == 2 && dxc[0].line == 7 && dxc[0].column == 12 && dxc[0].error, "DXC: line, column and error");
		Check(dxc.size() == 2 && dxc[0].text.find("float x = bar;") != std::string::npos, "DXC: the source excerpt stays with its message");
		Check(dxc.size() == 2 && dxc[1].line == 4 && !dxc[1].error, "DXC: a drive letter does not hide the line number");
	}

	void TestTools()
	{
		std::printf("Tools\n");
		const Json catalogue = ToolCatalogue();
		Check(catalogue.Has("get_frame_graph") && catalogue.Has("replace_shader"), "every MCP tool is offered");
		Check(catalogue["disable_shader"]["description"].AsString().find("Needs Debug Control") == std::string::npos,
			"without the MCP permission levels, which are not the assistant's");
		Check(catalogue["replace_shader"]["arguments"].Has("hlsl"), "with their arguments");
		Check(IsOperation("disable_shader") && IsOperation("replace_shader") && IsOperation("start_deep_capture"),
			"operations are the tools that change the game or write files");
		Check(!IsOperation("get_frame_graph") && !IsOperation("get_shader_disassembly"), "reads are not operations");
		Check(SendsShaderCode("get_shader_disassembly") && SendsShaderCode("get_shader_decompiled_hlsl") &&
			!SendsShaderCode("inspect_shader"), "the tools that send shader code are known");
		Check(!IsKnownTool("run_shell") && IsKnownTool("list_shaders"), "an invented tool is unknown");

		std::vector<ChatMessage> conversation;
		for (int i = 0; i < 20; ++i)
			conversation.push_back({ i % 2 == 0 ? "user" : "assistant", std::string(2000, static_cast<char>('a' + i)), {} });
		const Json history = ContextHistory(conversation);
		Check(history.Size() == 12, "the history keeps what fits in 24000 characters");
		Check(history.Size() == 12 && history.Items().back()["content"].AsString()[0] == 'a' + 19, "the newest last");
		conversation = { { "user", std::string(10000, 'x'), {} } };
		Check(ContextHistory(conversation).Items().front()["content"].AsString().size() == 6000, "a long message is cut to 6000");
	}

	void TestChatLoop()
	{
		std::printf("Chat loop\n");
		std::vector<std::string> called;
		AgentHost host;
		host.call_tool = [&](const std::string &tool, const Json &) {
			called.push_back(tool);
			Json answer = Json::Object();
			answer["ok"] = Json(true);
			answer["result"] = Parse("{\"passes\":[{\"name\":\"Bloom downsample\",\"confidence\":0.7}]}");
			return answer;
		};
		bool approve = true;
		int approvals = 0;
		host.approve = [&](const std::string &, const Json &, const Text &) { ++approvals; return approve; };
		host.application_state = []() { return Parse("{\"session\":{\"live\":true}}"); };

		AgentRequest request;
		request.text = "find the bloom";
		Cancellation cancel;

		{
			FakeProvider provider;
			provider.replies = { Step("reading the graph", "get_frame_graph"), Step("It is the Bloom downsample pass.", "reply") };
			const AgentReply reply = RunChat(provider, request, host, cancel);
			CheckEqual(reply.text, "It is the Bloom downsample pass.", "a tool, then the reply");
			Check(called.size() == 1 && called[0] == "get_frame_graph", "the tool ran once");
			const Json second = Parse(provider.contents.at(1));
			Check(second["tool_results"].Size() == 1 && second["tool_results"].Items()[0]["result"].Has("passes"),
				"its result goes back to the model");
			Check(second["remaining_tool_calls"].AsNumber() == 7.0, "with the remaining budget");
			Check(provider.systems.at(0).find("hypothesis") != std::string::npos, "the rules of the tool are in the prompt");
		}

		{
			called.clear();
			FakeProvider provider;
			provider.replies = { Step("off", "disable_shader", "{\"shader_id\":1}"),
				Step("off again", "disable_shader", "{\"shader_id\":2}"), Step("done", "reply") };
			RunChat(provider, request, host, cancel);
			Check(called.size() == 1, "automatic mode runs one operation per message");
			const Json third = Parse(provider.contents.at(2));
			Check(third["tool_results"].Items().back()["error"].AsString().find("already run") != std::string::npos,
				"and says so for the second one");
		}

		{
			called.clear();
			FakeProvider provider;
			for (int i = 0; i < 9; ++i)
				provider.replies.push_back(Step("list", "list_shaders"));
			const AgentReply reply = RunChat(provider, request, host, cancel);
			Check(called.size() == 8 && reply.text.empty() && English(reply.notice).find("Tool limit") != std::string::npos,
				"eight tool calls, then the limit");
		}

		{
			called.clear();
			FakeProvider provider;
			request.mode = PermissionMode::full;
			for (int i = 0; i < 3; ++i)
				provider.replies.push_back(Step("off", "disable_shader", "{\"shader_id\":1}"));
			provider.replies.push_back(Step("done", "reply"));
			RunChat(provider, request, host, cancel);
			Check(called.size() == 3, "full access allows several operations");
			request.mode = PermissionMode::automatic;
		}

		{
			called.clear();
			approvals = 0;
			FakeProvider provider;
			request.mode = PermissionMode::ask;
			provider.replies = { Step("reading the code", "get_shader_disassembly", "{\"shader_id\":4}"),
				Step("list", "list_shaders"), Step("off", "disable_shader", "{\"shader_id\":4}"), Step("never", "reply") };
			approve = true;
			host.approve = [&](const std::string &tool, const Json &, const Text &) {
				++approvals;
				return tool == "get_shader_disassembly";
			};
			const AgentReply reply = RunChat(provider, request, host, cancel);
			Check(approvals == 2, "ask each time: approvals for the code and for the operation, not for a list");
			Check(called.size() == 2 && called[0] == "get_shader_disassembly" && called[1] == "list_shaders",
				"what was allowed ran, what was refused did not");
			Check(English(reply.notice).find("denied") != std::string::npos, "a refusal ends the request and says so");
			request.mode = PermissionMode::automatic;
		}

		{
			FakeProvider provider;
			provider.replies = { Step("hack", "run_shell") };
			bool threw = false;
			try { RunChat(provider, request, host, cancel); }
			catch (const Error &) { threw = true; }
			Check(threw, "an unknown tool is refused");

			provider.replies = { Step("", "reply") };
			threw = false;
			try { RunChat(provider, request, host, cancel); }
			catch (const Error &) { threw = true; }
			Check(threw, "an empty reply is refused");
		}
	}

	void TestJobs()
	{
		std::printf("Shader jobs\n");
		Cancellation cancel;
		const Json shader = Parse("{\"description\":{\"stage\":\"pixel\"},\"disassembly\":\"ps_5_0\"}");

		FakeProvider provider;
		provider.replies.push_back(Parse("{\"message\":\"Downsamples the HDR colour.\",\"role\":\"Bloom\",\"name\":\"Bloom downsample\",\"confidence\":85}"));
		JobResult result = RunJob(provider, JobKind::classification, shader, "English", "en", cancel);
		Check(result.role == "Bloom" && result.name == "Bloom downsample", "a classification keeps its role and name");
		Check(result.confidence > 0.849f && result.confidence < 0.851f, "a confidence out of 100 becomes a fraction");
		Check(result.author == "fake-model", "the model is recorded");
		Check(provider.systems.back().find("Post Process") != std::string::npos, "the roles offered are the section 39 tags");

		provider.replies.push_back(Parse("{\"message\":\"?\",\"role\":\"Laser show\",\"confidence\":0.4}"));
		result = RunJob(provider, JobKind::classification, shader, "English", "en", cancel);
		Check(result.role == "Unknown" && result.confidence > 0.39f && result.confidence < 0.41f,
			"an invented role becomes Unknown, a fraction stays a fraction");

		provider.replies.push_back(Parse("{\"message\":\"Renamed.\",\"confidence\":70}"));
		bool threw = false;
		try { RunJob(provider, JobKind::cleanup, shader, "English", "en", cancel); }
		catch (const Error &) { threw = true; }
		Check(threw, "a clean-up without its HLSL is refused");

		provider.replies.push_back(Parse("{\"message\":\"Renamed.\",\"hlsl\":\"float4 main() : SV_Target { return 0; }\",\"confidence\":70}"));
		result = RunJob(provider, JobKind::cleanup, shader, "Français", "fr", cancel);
		Check(!result.hlsl.empty(), "a clean-up returns its HLSL");
		Check(provider.systems.back().find("Français (fr)") != std::string::npos, "and is asked in the interface language");
	}

	void TestHttp(FakeApi &api)
	{
		std::printf("OpenAI-compatible API over HTTP\n");
		ProviderConfig config;
		config.kind = ProviderKind::local_api;
		config.endpoint = api.Url();
		config.model = "test-model";
		config.key = "test-key-123";
		std::unique_ptr<Provider> provider = CreateProvider(config);
		Cancellation cancel;

		api.PushCompletion(Parse("{\"connected\":true}"));
		bool ok = true;
		try { provider->Check(cancel); }
		catch (const Error &) { ok = false; }
		Check(ok, "the connection test passes");
		const std::vector<FakeApi::Request> requests = api.Requests();
		Check(!requests.empty() && requests.back().head.find("Authorization: Bearer test-key-123") != std::string::npos,
			"the key goes in the Authorization header");
		const Json body = requests.empty() ? Json() : Parse(requests.back().body);
		Check(body["model"].AsString() == "test-model" && body["response_format"]["type"].AsString() == "json_object",
			"the model and the JSON mode are asked for");
		Check(body["messages"].Size() == 2 && body["messages"].Items()[0]["role"].AsString() == "system",
			"a system message and a user message");

		auto fails = [&](const char *expected) {
			try { provider->Complete("s", "c", cancel); }
			catch (const Error &error) { return English(error.Message()).find(expected) != std::string::npos; }
			return false;
		};
		api.Push({ 401, "{\"error\":\"no\"}", "", 0 });
		Check(fails("HTTP 401"), "an HTTP error is reported with its status");
		api.Push({ 302, "", "Location: https://elsewhere.example/\r\n", 0 });
		Check(fails("redirects"), "a redirect is refused, so the key is never forwarded");
		api.PushCompletion(Parse("{\"message\":\"cut\"}"), "length");
		Check(fails("complete"), "a truncated answer is refused");
		api.Push({ 200, "not json", "", 0 });
		Check(fails("Chat Completions"), "a body that is not a Chat Completions answer is refused");
	}

	// The whole assistant, threads included: connect, send, the tool queue pumped by "the UI".
	void TestAssistant(FakeApi &api)
	{
		std::printf("Assistant end to end\n");
		Assistant assistant;
		assistant.Config().kind = ProviderKind::local_api;
		assistant.Config().endpoint = api.Url();
		assistant.Config().model = "test-model";

		std::vector<std::string> called;
		const Assistant::ToolHandler handler = [&](const std::string &tool, const Json &) {
			called.push_back(tool);
			Json answer = Json::Object();
			answer["ok"] = Json(true);
			answer["result"] = Parse("{\"value\":42}");
			return answer;
		};
		auto run_until = [&](const std::function<bool()> &done) {
			for (int i = 0; i < 1000 && !done(); ++i)
			{
				assistant.Pump(handler);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			return done();
		};

		api.PushCompletion(Parse("{\"connected\":true}"));
		assistant.Connect();
		Check(run_until([&]() { return assistant.GetPhase() != Phase::connecting; }) && assistant.IsReady(),
			"connects to the provider");

		api.PushCompletion(Parse("{\"message\":\"reading\",\"action\":\"get_frame_graph\",\"arguments\":{}}"));
		api.PushCompletion(Parse("{\"message\":\"The bloom is pass 3.\",\"action\":\"reply\"}"));
		Check(assistant.Send("find the bloom", Parse("{\"shader_id\":7}"), "English", "en"), "a message is sent");
		Check(!assistant.Send("again", Json::Object(), "English", "en"), "a second one waits for the first");
		run_until([&]() { return !assistant.IsPending(); });
		std::vector<ChatMessage> conversation = assistant.Conversation();
		Check(conversation.size() == 2 && conversation[1].content == "The bloom is pass 3.", "the reply arrives");
		Check(std::find(called.begin(), called.end(), "get_frame_graph") != called.end() &&
			std::find(called.begin(), called.end(), "get_session") != called.end(),
			"its tools ran on the pumping thread, with the session as context");
		const std::vector<FakeApi::Request> requests = api.Requests();
		Check(requests.back().body.find("shader_id") != std::string::npos, "the selection is part of the context");

		// Ask each time, and a refusal.
		assistant.SetMode(PermissionMode::ask);
		called.clear();
		api.PushCompletion(Parse("{\"message\":\"off\",\"action\":\"disable_shader\",\"arguments\":{\"shader_id\":7}}"));
		assistant.Send("disable it", Json::Object(), "English", "en");
		Approval approval;
		Check(run_until([&]() { return assistant.PendingApproval(approval); }), "an operation asks for approval");
		Check(English(approval.summary).find("#7") != std::string::npos, "naming its target");
		assistant.Decide(approval.id, false);
		run_until([&]() { return !assistant.IsPending(); });
		conversation = assistant.Conversation();
		Check(std::find(called.begin(), called.end(), "disable_shader") == called.end(), "a refused operation never runs");
		Check(!conversation.empty() && English(conversation.back().notice).find("denied") != std::string::npos,
			"and the conversation says so");

		// Cancelling a request that is waiting on a slow provider.
		assistant.SetMode(PermissionMode::automatic);
		FakeApi::Reply slow;
		slow.delay_ms = 1500;
		Json message = Json::Object();
		message["role"] = Json("assistant");
		message["content"] = Json("{\"message\":\"late\",\"action\":\"reply\"}");
		slow.body = "{\"choices\":[{\"finish_reason\":\"stop\",\"message\":" + message.Write(-1) + "}]}";
		api.Push(slow);
		assistant.Send("slow", Json::Object(), "English", "en");
		assistant.CancelRequest();
		Check(!assistant.IsPending(), "cancelling frees the conversation at once");
		std::this_thread::sleep_for(std::chrono::milliseconds(2000));
		conversation = assistant.Conversation();
		Check(conversation.back().content != "late", "and a late answer is dropped");

		assistant.Disconnect();
		Check(!assistant.IsReady() && assistant.GetPhase() == Phase::disconnected, "disconnects");
	}

	void TestCli()
	{
		std::printf("CLI discovery (no call)\n");
		for (const char *name : { "codex", "claude" })
		{
			try
			{
				const std::wstring path = FindCli(name);
				std::printf("  info  %s found: %ls\n", name, path.c_str());
			}
			catch (const Error &error)
			{
				std::printf("  info  %s\n", English(error.Message()).c_str());
			}
		}
		Check(true, "discovery never throws anything but an Error");
	}

	// Only with --live-cli: one tiny request through each CLI that is installed and signed in, to
	// check the command lines still suit the installed versions. It uses the account's quota.
	void TestLiveCli()
	{
		std::printf("Live CLI requests (--live-cli)\n");
		for (ProviderKind kind : { ProviderKind::codex_cli, ProviderKind::claude_cli })
		{
			ProviderConfig config;
			config.kind = kind;
			try
			{
				std::unique_ptr<Provider> provider = CreateProvider(config);
				Cancellation cancel;
				std::printf("  info  %s\n", English(provider->Check(cancel)).c_str());
				const Json reply = provider->Complete("Return ONLY the JSON object {\"message\":\"pong\",\"action\":\"reply\"}.",
					"{\"request\":\"ping\"}", cancel);
				std::printf("  info  %s answered %s\n", provider->Describe().c_str(), reply.Write(-1).c_str());
				Check(reply["action"].AsString() == "reply", ProviderName(kind));
			}
			catch (const Error &error)
			{
				std::printf("  info  %s: %s\n", ProviderName(kind), English(error.Message()).c_str());
				Check(false, ProviderName(kind));
			}
		}
	}
}

int main(int argc, char **argv)
{
	std::printf("CyGPUInspectorAssistantTests\n\n");
	TestText();
	TestJsonReplies();
	TestEndpoints();
	TestQuoting();
	TestCompilerMessages();
	TestTools();
	TestChatLoop();
	TestJobs();

	FakeApi api;
	if (api.Start())
	{
		TestHttp(api);
		TestAssistant(api);
		api.Stop();
	}
	else
	{
		Check(false, "the loopback test server could not start");
	}
	TestCli();
	if (argc > 1 && std::string(argv[1]) == "--live-cli")
		TestLiveCli();

	std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
