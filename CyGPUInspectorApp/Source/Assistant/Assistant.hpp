// CyGPUInspectorApp — the assistant: connection, conversation, approvals and shader jobs.
//
// This class is what CyRSAssistant splits between its overlay and its companion service. Here the
// standalone is already a process of its own, outside the game, so the companion's work runs on
// worker threads of the standalone instead of in a second executable: the provider calls, the
// chat loop and the jobs never block the interface, and the game is never involved in them.
//
// The interface calls everything here from the UI thread. The worker threads only touch the shared
// state behind the mutex, and ask the UI thread to run each tool (Pump), because the tools read the
// session model, the frame graph and the analysis cache the way the MCP server's calls do.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "AssistantAgent.hpp"
#include "AssistantProvider.hpp"

#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cygi::assistant
{
	enum class Phase
	{
		disconnected,
		connecting,
		ready,
		error,
	};

	struct Approval
	{
		uint64_t id = 0;
		Text summary;
		std::string payload;   // the exact arguments, or the HLSL of a compilation
	};

	struct JobOutcome
	{
		uint32_t shader_id = 0;
		Sha256Digest signature;
		bool ok = false;
		JobResult result;
		Text error;
	};

	class Assistant
	{
	public:
		Assistant();
		~Assistant();
		Assistant(const Assistant &) = delete;
		Assistant &operator=(const Assistant &) = delete;

		// ---- connection -------------------------------------------------------------------------
		// Edited by the Connection tab while disconnected; read when Connect is pressed.
		ProviderConfig &Config() { return m_config; }
		bool Wanted() const;
		void Connect();
		void Disconnect();
		Phase GetPhase() const;
		Text StatusMessage() const;
		bool IsReady() const;
		std::string ProviderDescription() const;

		// ---- chat --------------------------------------------------------------------------------
		PermissionMode Mode() const;
		// Changing the mode cancels a request in progress, as in CyRSAssistant.
		void SetMode(PermissionMode mode);

		std::vector<ChatMessage> Conversation() const;
		uint64_t ConversationVersion() const;   // changes whenever a message is added

		bool IsPending() const;
		uint64_t PendingSeconds() const;
		std::string Progress() const;

		// `selection` is what is selected in the interface, given to the model as context.
		bool Send(const std::string &text, const Json &selection, const std::string &language_name,
		          const std::string &language_code);
		void CancelRequest();
		void NewConversation();

		bool PendingApproval(Approval &out) const;
		void Decide(uint64_t approval_id, bool allow);

		// ---- shader jobs -------------------------------------------------------------------------
		bool IsJobRunning() const;
		uint32_t JobShader() const;
		JobKind RunningJob() const;
		uint64_t JobSeconds() const;
		bool StartJob(JobKind kind, uint32_t shader_id, const Sha256Digest &signature, Json shader,
		              const std::string &language_name, const std::string &language_code);
		void CancelJob();
		bool TakeJobOutcome(JobOutcome &out);

		// ---- UI thread ---------------------------------------------------------------------------
		// Runs the tool call the chat loop is waiting for, if there is one. `handler` returns the
		// standalone's answer, the same one an MCP client gets.
		using ToolHandler = std::function<Json(const std::string &tool, const Json &arguments)>;
		void Pump(const ToolHandler &handler);

		// The state the worker threads share with the interface. Declared here for them only.
		struct Shared;

	private:
		std::shared_ptr<Shared> m_shared;
		ProviderConfig m_config;
	};
}
