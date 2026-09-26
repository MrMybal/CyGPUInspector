// CyGPUInspectorApp — the model the assistant talks to.
//
// The same four providers as CyRSAssistant, driven the same way:
//
//   Codex (CLI session)        `codex exec`, read-only sandbox, no shell, no user config
//   Claude Code (CLI session)  `claude -p`, no tools, no MCP server, no hooks, no saved session
//   OpenAI-compatible API      a /chat/completions URL, a model and a key if the server wants one
//   Local model (API)          the same, on localhost
//
// Every provider is used the same way: one system prompt and one JSON document in, one JSON object
// out. The tools are not the CLI's: the assistant runs its own bounded loop over CyGPUInspector's
// tools and gives the CLI nothing it could act with. A CLI uses the login it already has; an API
// key stays in memory for the session and is never written to disk.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

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
