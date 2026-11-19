// CyGPUInspectorApp — the model the assistant talks to.
//
// The existing four provider choices are implemented by CyMCPSDK Conversation.
// Codex and HTTP use native tool calls; Claude retains the bounded host JSON loop.
// CLI authentication belongs to the installed agent. API keys remain in memory.
// Optional private checkpoints retain full tool exchanges separately from model context projection.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once
#include <cymcp/ai.hpp>

#include "AssistantText.hpp"

#include <CyGPUInspectorCore/Json.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

namespace cygi::assistant
{
	enum class ProviderKind : int
	{
		codex_cli = 0,
		claude_cli,
		openai_api,
		local_api,
		count,
	};

	// English, for the interface to translate.
	const char *ProviderName(ProviderKind kind);
	inline bool IsCli(ProviderKind kind) { return kind == ProviderKind::codex_cli || kind == ProviderKind::claude_cli; }

	struct ProviderConfig
	{
		ProviderKind kind = ProviderKind::codex_cli;
        bool deferred_tools = true;
        bool remember_history = false;
        std::filesystem::path history_directory; // optional host override, never sent to providers
        int context_bytes = 0;
		std::string model;       // empty: the CLI's default model
		std::string endpoint;    // full /chat/completions URL, API providers only
		std::string key;         // API key, in memory only
	};

	// What every provider failure is reported as: a message for the person, never the CLI's own
	// error output, which can contain account details.
	class Error : public std::runtime_error
	{
	public:
		explicit Error(Text message) : std::runtime_error(English(message)), m_message(std::move(message)) {}
		const Text &Message() const { return m_message; }

	private:
		Text m_message;
	};

	// Shared by a request and whoever wants to stop it. Cancelling ends a CLI at once, with every
	// process it started; an HTTP request is abandoned and its answer ignored.
	class Cancellation
	{
	public:
		~Cancellation();
		void Cancel();
		bool IsCancelled() const { return m_cancelled.load(); }
        cymcp::Cancellation sdk;
		// The job object of a running CLI, terminated by Cancel. Ownership stays with the caller.
		void Attach(void *job);
		void Detach();

	private:
		std::mutex m_mutex;
		std::atomic<bool> m_cancelled{ false };
		void *m_job = nullptr;
	};

	class Provider
	{
	public:
		virtual ~Provider() = default;
        virtual void ResetHistory() {}
        virtual Json RestoredHistory() const { return Json::Array(); }
        virtual bool UsesNativeTools() const { return false; }
        virtual std::string NativeChat(std::shared_ptr<const cymcp::Registry>, const std::string&, const std::string&, Cancellation&, const std::function<void(const std::string&)>&) { throw Error("Native tools unavailable."); }


		// What Connect checks. A CLI: its own login status, without calling the model. An API: a
		// small request the model has to answer with the expected JSON.
		virtual Text Check(Cancellation &cancel) = 0;

		// One request, one JSON object back. Throws Error.
		virtual Json Complete(const std::string &system, const std::string &content, Cancellation &cancel) = 0;

		// "Codex CLI (default model)", "Claude Code CLI (sonnet)", "qwen3-coder": stored with every
		// AI annotation, so a result can be told apart and regenerated later (section 58).
		virtual std::string Describe() const = 0;
	};

	// Throws Error when the configuration cannot work: CLI missing, URL refused.
	std::unique_ptr<Provider> CreateProvider(const ProviderConfig &config);

	// ---- exposed for the tests ------------------------------------------------------------------

	// A model reply as a JSON object: bare, inside a ``` fence, or with a sentence around it.
	bool ParseJsonReply(const std::string &raw, Json &out);

	// HTTPS, or HTTP on localhost for a local model; no credentials in the URL. Throws Error.
	void ValidateEndpoint(const std::string &url);

	// One argument quoted for CreateProcess, so that CommandLineToArgvW gives it back unchanged.
	std::wstring QuoteArgument(const std::wstring &argument);

	// The executable of a CLI: on the PATH, then where npm and the Claude installer put it.
	// Throws Error when it is nowhere.
	std::wstring FindCli(const char *name);

	struct HttpResponse
	{
		unsigned long status = 0;
		std::string body;
	};

	// POST of a JSON body. Redirects are refused: following one would forward the key to wherever
	// the server pointed. Throws Error on a transport failure or a body above 2 MiB.
	HttpResponse HttpPostJson(const std::string &url, const std::string &body, const std::string &key,
	                          unsigned long timeout_ms, Cancellation &cancel);
}
