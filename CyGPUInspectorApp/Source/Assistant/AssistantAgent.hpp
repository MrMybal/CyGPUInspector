// CyGPUInspectorApp — what the assistant does with a provider: the chat loop and the shader jobs.
//
// The chat loop is CyRSAssistant's, applied to CyGPUInspector's tools. The model answers with one
// JSON object: either a reply, or one tool to call with its arguments. The tool runs, its result
// goes back to the model with the rest of the context, and so on, within a fixed budget: eight
// tool calls and one operation per message, sixteen and eight in full access. The tools are the
// ones CyGPUInspectorMCP publishes, handled by the same code in the standalone.
//
// The shader jobs are the assisted work of milestone 9 (HLSL clean-up, classification,
// explanation). They get the shader's data up front and no tools: one request, one answer, stored
// as an annotation labelled with its model and prompt version.
//
// Nothing here knows about the interface: the host gives the loop its tools, its approvals and a
// place to report progress, which is what lets the tests drive it without a window.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "AssistantProvider.hpp"

#include <CyGPUInspectorCore/Json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace cygi::assistant
{
	// The version stored with every AI annotation. Change it when a prompt changes in a way that
	// makes older results worth regenerating (section 58).
	constexpr const char *kPromptVersion = "cygi-assistant-1";

	enum class PermissionMode : int
	{
		automatic = 0,   // requested reads and actions run without an extra click
		ask,             // each operation, and each shader code sent to the provider, is approved
		full,            // several operations in a row, no intermediate approval
		count,
	};
	const char *PermissionModeName(PermissionMode mode);   // English, for Tr

	struct ChatMessage
	{
		std::string role;       // "user", "assistant" or "information"
		std::string content;    // what was said, verbatim
		Text notice;            // an information message of the application, translated when shown
	};

	// ---- the tools --------------------------------------------------------------------------------

	// Every tool with its description and arguments, as the model is given them.
	Json ToolCatalogue();
	bool IsKnownTool(const std::string &tool);
	// Changes the running game or writes files: counted against the operation budget.
	bool IsOperation(const std::string &tool);
	// Sends a shader's code to the provider: approved one by one in "ask each time".
	bool SendsShaderCode(const std::string &tool);

	// What an approval shows: one sentence, and the exact arguments (the HLSL of a replacement).
	Text DescribeAction(const std::string &tool, const Json &arguments);

	// The last exchanges sent with a request: 16 messages at most, 24 000 characters, 6 000 per
	// message. The request itself is sent apart.
	Json ContextHistory(const std::vector<ChatMessage> &conversation);

	// ---- the chat loop ----------------------------------------------------------------------------

	struct AgentHost
	{
		// Runs a tool and returns the standalone's answer, {"ok":true,"result":...} or
		// {"ok":false,"error":"..."}. Blocks until it has run.
		std::function<Json(const std::string &tool, const Json &arguments)> call_tool;
		// Asks the person, in "ask each time" only. Blocks; false means refused or cancelled.
		std::function<bool(const std::string &tool, const Json &arguments, const Text &summary)> approve;
		// The step under way, shown under "Working...".
		std::function<void(const std::string &progress)> progress;
		// The session, the frame and what is selected in the interface, refreshed at each step.
		std::function<Json()> application_state;
	};

	struct AgentRequest
	{
		std::string text;
		std::vector<ChatMessage> conversation;   // before this request
		PermissionMode mode = PermissionMode::automatic;
		std::string language_name = "English";
		std::string language_code = "en";
	};

	// The final answer: the model's reply, or a notice of the application (limit reached, action
	// refused). Throws Error when the provider fails or answers something unusable.
	struct AgentReply
	{
		std::string text;
		Text notice;
	};
	AgentReply RunChat(Provider &provider, const AgentRequest &request, AgentHost &host, Cancellation &cancel);

	std::string ChatSystemPrompt();
	std::string ResponseInstructions(const std::string &language_name, const std::string &language_code);

	// ---- the shader jobs --------------------------------------------------------------------------

	enum class JobKind : int
	{
		cleanup = 0,
		classification,
		explanation,
	};

	struct JobResult
	{
		JobKind kind = JobKind::explanation;
		std::string message;       // what the model says, in the interface language
		std::string hlsl;          // cleanup only: the rewritten HLSL
		std::string role;          // classification only: one of the section 39 tags
		std::string name;          // classification only: a short name for the shader
		float confidence = 0.0f;   // 0 to 1
		std::string author;        // the provider and model that produced it
	};

	// `shader` holds what the standalone knows: description, bindings, disassembly and the
	// reconstructions of each decompiler with their verdicts.
	JobResult RunJob(Provider &provider, JobKind kind, const Json &shader, const std::string &language_name,
	                 const std::string &language_code, Cancellation &cancel);
	std::string JobSystemPrompt(JobKind kind);
}
