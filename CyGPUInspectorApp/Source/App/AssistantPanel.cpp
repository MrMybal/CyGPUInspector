// CyGPUInspectorApp — the AI assistant panel and the AI Analysis tab of the shader code panel.
//
// The panel is CyRSAssistant's: a status line, then Chat, Options and Connection. The chat talks to
// the provider chosen in Connection, which works on the frame with CyGPUInspector's own tools. The
// AI Analysis tab runs the three shader jobs of milestone 9 on the selected shader and keeps their
// results as annotations, labelled with the model that produced them.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"
#include "LayoutHelpers.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;
	using assistant::Text;

	namespace
	{
		const ImVec4 kAiColour(0.8f, 0.7f, 1.0f, 1.0f);
		const ImVec4 kUserColour(0.5f, 0.75f, 1.0f, 1.0f);
		const ImVec4 kAssistantColour(0.65f, 0.9f, 0.65f, 1.0f);
		const ImVec4 kInformationColour(0.9f, 0.8f, 0.5f, 1.0f);
		const ImVec4 kWarningColour(1.0f, 0.6f, 0.3f, 1.0f);

		constexpr const char *kFenceStart = "```hlsl\n";

		std::string LanguageName(const std::string &code)
		{
			for (const i18n::LanguageInfo &language : i18n::Languages())
				if (language.code == code)
					return language.name;
			return code;
		}

		std::string NowText()
		{
			SYSTEMTIME now = {};
			GetLocalTime(&now);
			char text[32];
			std::snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u", now.wYear, now.wMonth, now.wDay,
				now.wHour, now.wMinute);
			return text;
		}

		Json ToolRequest(const std::string &tool, const Json &arguments)
		{
			Json request = Json::Object();
			request["tool"] = Json(tool);
			request["arguments"] = arguments;
			return request;
		}

		std::string Cut(const std::string &text, size_t limit, bool &cut)
		{
			cut = text.size() > limit;
			if (!cut)
				return text;
			size_t end = limit;
			while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
				--end;
			return text.substr(0, end);
		}

		bool IsPending(const Json &answer)
		{
			return !answer["ok"].AsBool() && answer["error"].AsString().find("ask again in a moment") != std::string::npos;
		}

		const char *JobLabel(assistant::JobKind kind)
		{
			switch (kind)
			{
			case assistant::JobKind::cleanup: return "HLSL clean-up";
			case assistant::JobKind::classification: return "Classification";
			case assistant::JobKind::explanation:
			default: return "Explanation";
			}
		}

		AnnotationKind AnnotationKindOf(assistant::JobKind kind)
		{
			switch (kind)
			{
			case assistant::JobKind::cleanup: return AnnotationKind::cleanup;
			case assistant::JobKind::classification: return AnnotationKind::classification;
			case assistant::JobKind::explanation:
			default: return AnnotationKind::explanation;
			}
		}

		// An AI clean-up is stored as its summary followed by the code in a fence, so the annotation
		// stays one readable text in notes.json and the code can still be taken back out of it.
		bool SplitCleanup(const std::string &text, std::string &summary, std::string &hlsl)
		{
			const size_t open = text.find(kFenceStart);
			if (open == std::string::npos)
				return false;
			const size_t start = open + std::strlen(kFenceStart);
			const size_t close = text.rfind("\n```");
			if (close == std::string::npos || close < start)
				return false;
			summary = text.substr(0, open);
			while (!summary.empty() && (summary.back() == '\n' || summary.back() == ' '))
				summary.pop_back();
			hlsl = text.substr(start, close - start);
			return true;
		}
	}

	// ------------------------------------------------------------------------------------------
	// Preferences: provider, model, URL and permission mode. Never the key.
	// ------------------------------------------------------------------------------------------

	void Application::LoadAssistantPreferences()
	{
		const Json root = LoadPreferences();
		const Json &saved = root["assistant"];

		assistant::ProviderConfig &config = m_assistant.Config();
		const int provider = static_cast<int>(saved["provider"].AsNumber(0.0));
		config.kind = static_cast<assistant::ProviderKind>(std::clamp(provider, 0, static_cast<int>(assistant::ProviderKind::count) - 1));
		config.model = saved["model"].AsString();
		config.endpoint = saved["endpoint"].AsString();

		m_assistant_provider = static_cast<int>(config.kind);
		std::snprintf(m_assistant_model, sizeof(m_assistant_model), "%s", config.model.c_str());
		std::snprintf(m_assistant_endpoint, sizeof(m_assistant_endpoint), "%s", config.endpoint.c_str());

		const int mode = static_cast<int>(saved["permission_mode"].AsNumber(0.0));
		m_assistant.SetMode(static_cast<assistant::PermissionMode>(std::clamp(mode, 0, 2)));
	}

	void Application::SaveAssistantPreferences()
	{
		Json saved = Json::Object();
		saved["provider"] = Json(m_assistant_provider);
		saved["model"] = Json(std::string(m_assistant_model));
		saved["endpoint"] = Json(std::string(m_assistant_endpoint));
		saved["permission_mode"] = Json(static_cast<int>(m_assistant.Mode()));

		Json root = LoadPreferences();
		root["assistant"] = saved;
		SavePreferences(root);
	}

	void Application::ConnectAssistant()
	{
		assistant::ProviderConfig &config = m_assistant.Config();
		config.kind = static_cast<assistant::ProviderKind>(m_assistant_provider);
		config.model = m_assistant_model;
		config.endpoint = m_assistant_endpoint;
		config.key = m_assistant_key;
		SaveAssistantPreferences();
		m_assistant.Connect();
	}

	// ------------------------------------------------------------------------------------------
	// Per frame: the tools the loop waits for, the data a job waits for, the results that came back.
	// ------------------------------------------------------------------------------------------

	Json Application::AssistantSelection() const
	{
		Json selection = Json::Object();
		selection["viewing_capture"] = Json(m_capture_model != nullptr);
		if (m_selected_shader != 0)
			selection["shader_id"] = Json(m_selected_shader);
		if (m_selected_resource != 0)
			selection["resource_id"] = Json(m_selected_resource);
		if (m_selected_event != 0)
			selection["event_index"] = Json(m_selected_event);
		return selection;
	}

	void Application::UpdateAssistant(double delta_seconds)
	{
		m_assistant.Pump([this](const std::string &tool, const Json &arguments) {
			return HandleMcpRequest(ToolRequest(tool, arguments));
		});

		if (m_ai_request.active)
		{
			m_ai_request.waited += delta_seconds;
			m_ai_request.since_try += delta_seconds;
			if (m_ai_request.since_try >= 0.25)
			{
				m_ai_request.since_try = 0.0;
				Json data;
				Sha256Digest signature;
				std::string error;
				bool wait = false;
				if (GatherShaderData(m_ai_request.shader_id, data, signature, error, wait))
				{
					const std::string code = i18n::CurrentLanguage();
					if (!m_assistant.StartJob(m_ai_request.kind, m_ai_request.shader_id, signature, std::move(data),
						LanguageName(code), code))
						m_ai_notice = "The assistant is not connected, or a job is already running.";
					m_ai_request.active = false;
				}
				else if (!wait || m_ai_request.waited > 20.0)
				{
					m_ai_notice = Text::Format("The data of this shader is not available: %s",
						{ Text::Literal(wait ? std::string("the analysis is taking too long") : error) });
					m_ai_request.active = false;
				}
			}
		}

		assistant::JobOutcome outcome;
		while (m_assistant.TakeJobOutcome(outcome))
			StoreJobOutcome(outcome);
	}

	void Application::RequestShaderJob(assistant::JobKind kind, uint32_t shader_id)
	{
		m_ai_request = AiRequest();
		m_ai_request.active = true;
		m_ai_request.kind = kind;
		m_ai_request.shader_id = shader_id;
		m_ai_request.since_try = 1.0;   // try at once
		m_ai_notice = {};
	}

	// Everything the standalone knows about a shader, the way an MCP agent would read it: the
	// description with its bindings, the disassembly and each decompiler's reconstruction.
	bool Application::GatherShaderData(uint32_t shader_id, Json &out, Sha256Digest &signature, std::string &error,
	                                   bool &wait)
	{
		wait = false;
		{
			SessionModel *active = ActiveModel();
			if (active == nullptr)
			{
				error = "no session is connected and no capture is open";
				return false;
			}
			std::lock_guard<std::mutex> lock(active->Mutex());
			const ShaderInfo *shader = active->ShaderById(shader_id);
			if (shader == nullptr)
			{
				error = "this shader is no longer known";
				return false;
			}
			signature = shader->signature;
		}

		Json arguments = Json::Object();
		arguments["shader_id"] = Json(shader_id);
		const Json description = HandleMcpRequest(ToolRequest("inspect_shader", arguments));
		if (!description["ok"].AsBool())
		{
			error = description["error"].AsString();
			return false;
		}
		const Json disassembly = HandleMcpRequest(ToolRequest("get_shader_disassembly", arguments));
		const Json reconstructions = HandleMcpRequest(ToolRequest("get_shader_decompiled_hlsl", arguments));
		if (IsPending(disassembly) || IsPending(reconstructions))
		{
			wait = true;
			return false;
		}

		out = Json::Object();
		out["description"] = description["result"];

		bool cut = false;
		if (disassembly["ok"].AsBool())
		{
			out["disassembler"] = disassembly["result"]["tool"];
			out["disassembly"] = Json(Cut(disassembly["result"]["disassembly"].AsString(), 80000, cut));
			if (cut)
				out["disassembly_note"] = Json("cut to its first 80000 characters");
		}
		else
		{
			out["disassembly_error"] = disassembly["error"];
		}

		if (reconstructions["ok"].AsBool())
		{
			Json list = Json::Array();
			for (const Json &item : reconstructions["result"]["reconstructions"].Items())
			{
				Json entry = Json::Object();
				entry["backend"] = item["backend"];
				entry["version"] = item["version"];
				entry["verdict"] = item["verdict"];
				entry["notes"] = Json(Cut(item["notes"].AsString(), 2000, cut));
				entry["hlsl"] = Json(Cut(item["hlsl"].AsString(), 40000, cut));
				if (cut)
					entry["hlsl_note"] = Json("cut to its first 40000 characters");
				list.Push(entry);
				if (list.Size() == 4)
					break;
			}
			out["reconstructions"] = list;
			out["warning"] = reconstructions["result"]["warning"];
		}
		else
		{
			out["reconstructions_error"] = reconstructions["error"];
		}
		return true;
	}

	void Application::StoreJobOutcome(const assistant::JobOutcome &outcome)
	{
		const assistant::JobResult &result = outcome.result;
		if (!outcome.ok)
		{
			m_ai_notice = Text::Format("%s failed: %s", { Text(JobLabel(result.kind)), outcome.error });
			return;
		}

		Annotation annotation;
		annotation.source = AnnotationSource::ai;
		annotation.kind = AnnotationKindOf(result.kind);
		annotation.author = result.author;
		annotation.prompt_version = assistant::kPromptVersion;
		annotation.created = NowText();
		annotation.confidence = result.confidence;
		switch (result.kind)
		{
		case assistant::JobKind::cleanup:
			annotation.text = result.message + "\n\n" + kFenceStart + result.hlsl + "\n```";
			break;
		case assistant::JobKind::classification:
			annotation.text = "Likely role: " + result.role + (result.name.empty() ? std::string() : " (" + result.name + ")") +
				"\n\n" + result.message;
			break;
		case assistant::JobKind::explanation:
		default:
			annotation.text = result.message;
			break;
		}

		// Loaded and saved by signature: the result belongs to the shader, whatever is selected now.
		ShaderNotes notes;
		LoadShaderNotes(m_analysis.Store(), outcome.signature, notes);
		notes.annotations.push_back(std::move(annotation));
		SaveShaderNotes(m_analysis.Store(), outcome.signature, notes);
		if (m_notes_shader != 0 && m_notes_signature == outcome.signature)
			m_notes = notes;
		m_ai_preview_index = -1;

		m_ai_notice = Text::Format("%s of shader #%s is ready.",
			{ Text(JobLabel(result.kind)), Text::Literal(std::to_string(outcome.shader_id)) });
	}

	// ------------------------------------------------------------------------------------------
	// The panel
	// ------------------------------------------------------------------------------------------

	void Application::DrawAssistantPanel()
	{
		using namespace assistant;

		// A layout saved before this panel existed has no place for it: it joins Details the first
		// time instead of floating over the frame.
		if (ImGuiWindow *details = ImGui::FindWindowByName(TrId("Details")); details != nullptr && details->DockId != 0)
			ImGui::SetNextWindowDockID(details->DockId, ImGuiCond_FirstUseEver);
		// Not while a fresh default layout is still choosing the tab of each group: it would win.
		if (m_assistant_focus && !m_picking_tabs)
		{
			ImGui::SetNextWindowFocus();
			m_assistant_focus = false;
		}
		if (!ImGui::Begin(TrId("AI assistant")))
		{
			ImGui::End();
			return;
		}

		const bool wanted = m_assistant.Wanted();
		const bool ready = m_assistant.IsReady();
		const Phase phase = m_assistant.GetPhase();

		if (m_logo != nullptr)
		{
			const float size = ImGui::GetTextLineHeight() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
			ImGui::Image(reinterpret_cast<ImTextureID>(m_logo), ImVec2(size, size));
			ImGui::SameLine();
		}
		ImGui::BeginGroup();
		ImGui::TextUnformatted(Tr("CyGPUInspector - AI assistant"));
		const ImVec4 colour = ready ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
			: phase == Phase::error ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(1.0f, 0.8f, 0.35f, 1.0f);
		ImGui::TextColored(colour, "%s", ready ? Tr("READY") : phase == Phase::connecting ? Tr("CONNECTING")
			: phase == Phase::error ? Tr("CONNECTION ERROR") : Tr("DISCONNECTED"));
		ImGui::EndGroup();
		ImGui::TextWrapped("%s", Render(m_assistant.StatusMessage()).c_str());
		if (!wanted && ImGui::Button(TrId("Connect to provider")))
			ConnectAssistant();
		ImGui::TextDisabled(Tr("Permissions: %s"), Tr(PermissionModeName(m_assistant.Mode())));

		if (ImGui::BeginTabBar("assistant_tabs"))
		{
			if (ImGui::BeginTabItem(TrId("Chat")))
			{
				DrawAssistantChat(ready);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(TrId("Options")))
			{
				ImGui::SeparatorText(Tr("Built-in assistant permissions"));
				const char *names[] = { Tr(PermissionModeName(PermissionMode::automatic)),
					Tr(PermissionModeName(PermissionMode::ask)), Tr(PermissionModeName(PermissionMode::full)) };
				int mode = static_cast<int>(m_assistant.Mode());
				if (ImGui::Combo(TrId("Permission mode"), &mode, names, IM_ARRAYSIZE(names)))
				{
					m_assistant.SetMode(static_cast<PermissionMode>(mode));
					SaveAssistantPreferences();
				}
				ImGui::TextWrapped(Tr("Automatic: requested reads and actions need no extra approval. One operation "
				                      "on the game per message."));
				ImGui::TextWrapped(Tr("Ask each time: the frame, passes, resources and timings can be read directly. "
				                      "Approve each operation on the game, and each shader disassembly or HLSL, before "
				                      "it is sent to the provider."));
				ImGui::TextWrapped(Tr("Full access: the assistant can use the read tools and several operations in a "
				                      "row to complete your request, without intermediate approvals."));
				ImGui::TextWrapped(Tr("These modes cover CyGPUInspector's tools: reading the frame, disabling, "
				                      "highlighting, capturing and replacing shaders. They never give a terminal or "
				                      "access to your files. Changing the mode cancels a pending request. External "
				                      "agents connected through MCP use the permission level of the MCP panel instead."));
				ImGui::SeparatorText(Tr("Language"));
				ImGui::TextWrapped(Tr("The assistant answers in the interface language: View > Language. A request "
				                      "already in progress finishes in its original language."));
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(TrId("Connection")))
			{
				ImGui::SeparatorText(Tr("Provider connection"));
				ImGui::BeginDisabled(wanted);
				const char *providers[] = { Tr(ProviderName(ProviderKind::codex_cli)), Tr(ProviderName(ProviderKind::claude_cli)),
					Tr(ProviderName(ProviderKind::openai_api)), Tr(ProviderName(ProviderKind::local_api)) };
				ImGui::Combo(TrId("Provider"), &m_assistant_provider, providers, IM_ARRAYSIZE(providers));
				const bool cli = IsCli(static_cast<ProviderKind>(m_assistant_provider));
				ImGui::InputText(cli ? TrId("Model (empty = CLI default)") : TrId("Model"), m_assistant_model,
					sizeof(m_assistant_model));
				if (!cli)
				{
					const bool local = static_cast<ProviderKind>(m_assistant_provider) == ProviderKind::local_api;
					ImGui::InputTextWithHint(TrId("URL /chat/completions"),
						local ? "http://localhost:11434/v1/chat/completions" : "https://api.example.com/v1/chat/completions",
						m_assistant_endpoint, sizeof(m_assistant_endpoint));
					ImGui::InputText(TrId("API key"), m_assistant_key, sizeof(m_assistant_key), ImGuiInputTextFlags_Password);
					ImGui::TextDisabled(Tr("The key is kept in memory for this session only."));
				}
				else
				{
					ImGui::TextWrapped(Tr("Uses your existing CLI login. Connect checks the local session; the first "
					                      "message checks model access."));
				}
				ImGui::EndDisabled();

				if (!wanted)
				{
					if (ImGui::Button(TrId("Connect")))
						ConnectAssistant();
				}
				else if (ImGui::Button(TrId("Disconnect / edit")))
				{
					m_assistant.Disconnect();
				}

				ImGui::SeparatorText(Tr("What is sent"));
				ImGui::TextWrapped(Tr("Your messages, and the data the assistant reads with its tools: the structure of "
				                      "the frame, names, timings and, when it asks for them, shader code. Never your "
				                      "files. With a CLI or a remote API, this data goes to that provider; with a local "
				                      "model, it stays on this machine."));
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
	}

	void Application::DrawAssistantChat(bool ready)
	{
		using namespace assistant;
		const bool pending = m_assistant.IsPending();

		ImGui::BeginDisabled(pending);
		if (ImGui::SmallButton(TrId("New conversation")))
		{
			m_assistant.NewConversation();
			m_assistant_input[0] = '\0';
		}
		ImGui::EndDisabled();
		SameLineIfFits(TrId("Copy conversation"));

		const std::vector<ChatMessage> conversation = m_assistant.Conversation();
		auto label = [](const ChatMessage &message) {
			return message.role == "user" ? Tr("You") : message.role == "assistant" ? Tr("Assistant") : Tr("Information");
		};
		auto body = [](const ChatMessage &message) {
			return message.content.empty() ? Render(message.notice) : message.content;
		};

		if (ImGui::SmallButton(TrId("Copy conversation")))
		{
			std::string transcript;
			for (const ChatMessage &message : conversation)
				transcript += std::string(label(message)) + ": " + body(message) + "\n\n";
			ImGui::SetClipboardText(transcript.c_str());
		}
		SameLineIfFits(TrId("About chat history"));
		if (ImGui::SmallButton(TrId("About chat history")))
			ImGui::OpenPopup("chat_privacy");
		if (ImGui::BeginPopup("chat_privacy"))
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
			ImGui::TextUnformatted(Tr("32 messages kept in memory. Recent exchanges are sent to the selected provider, "
			                          "including after switching providers. New conversation clears this local context "
			                          "without undoing what was done in the game."));
			ImGui::PopTextWrapPos();
			ImGui::EndPopup();
		}

		Approval approval;
		const bool approval_pending = m_assistant.PendingApproval(approval);
		// Everything below the conversation has to stay on screen, however small the panel is docked:
		// the message field gives up height first, then the conversation, never the Send button.
		const float line = ImGui::GetFrameHeightWithSpacing();
		const float available = ImGui::GetContentRegionAvail().y;
		const float input_height = std::clamp(available * 0.22f, line * 2.2f, 90.0f);
		const float below = input_height + line * 4.6f + (approval_pending ? line * 4.0f : 0.0f);
		ImGui::BeginChild("conversation", ImVec2(0.0f, std::max(line * 3.0f, available - below)),
			ImGuiChildFlags_Borders);
		if (conversation.empty())
			ImGui::TextWrapped(Tr("Hello! Ask a question about the frame: which pass is the bloom, what a shader does, "
			                      "why a pass is slow. The assistant reads the data itself with CyGPUInspector's tools."));
		for (size_t i = 0; i < conversation.size(); ++i)
		{
			const ChatMessage &message = conversation[i];
			ImGui::PushID(static_cast<int>(i));
			const ImVec4 colour = message.role == "user" ? kUserColour
				: message.role == "assistant" ? kAssistantColour : kInformationColour;
			ImGui::TextColored(colour, "%s", label(message));
			ImGui::SameLine();
			const std::string text = body(message);
			if (ImGui::SmallButton(TrId("Copy")))
				ImGui::SetClipboardText(text.c_str());
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::Separator();
			ImGui::PopID();
		}
		if (pending)
		{
			ImGui::Text(Tr("Working... %llu s"), static_cast<unsigned long long>(m_assistant.PendingSeconds()));
			const std::string progress = m_assistant.Progress();
			if (!progress.empty())
				ImGui::TextWrapped("%s", progress.c_str());
		}
		const uint64_t version = m_assistant.ConversationVersion();
		if (version != m_assistant_seen_version || pending)
		{
			if (version != m_assistant_seen_version || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
				ImGui::SetScrollHereY(1.0f);
			m_assistant_seen_version = version;
		}
		ImGui::EndChild();

		if (approval_pending)
		{
			ImGui::SeparatorText(Tr("Approval requested"));
			ImGui::TextWrapped("%s", Render(approval.summary).c_str());
			if (ImGui::CollapsingHeader(TrId("View proposed values or code")))
			{
				ImGui::BeginChild("permission_details", ImVec2(0.0f, 130.0f), ImGuiChildFlags_Borders);
				ImGui::TextUnformatted(approval.payload.c_str());
				ImGui::EndChild();
			}
			if (ImGui::Button(TrId("Allow this action")))
				m_assistant.Decide(approval.id, true);
			ImGui::SameLine();
			if (ImGui::Button(TrId("Deny this action")))
				m_assistant.Decide(approval.id, false);
		}

		// The questions of section 84 of the brief, one click away. They fill the message; sending
		// stays a separate decision.
		ImGui::TextDisabled(Tr("Examples:"));
		SameLineIfFits(TrId("Find the bloom"));
		const char *example = nullptr;
		if (ImGui::SmallButton(TrId("Find the bloom")))
			example = Tr("Which part of the rendering seems to produce the bloom? Give the passes, shaders, "
			             "resources and timings, with your reasoning.");
		SameLineIfFits(TrId("Explain the pipeline"));
		if (ImGui::SmallButton(TrId("Explain the pipeline")))
			example = Tr("Explain how this frame is rendered, pass by pass, from the frame graph and the timings.");
		SameLineIfFits(TrId("The selected shader"));
		if (ImGui::SmallButton(TrId("The selected shader")))
			example = Tr("What does the selected shader do, and which pass is it part of?");
		if (example != nullptr)
		{
			std::snprintf(m_assistant_input, sizeof(m_assistant_input), "%s", example);
			m_assistant_refocus = true;
		}

		ImGui::TextUnformatted(Tr("Your message"));
		if (m_assistant_refocus)
		{
			ImGui::SetKeyboardFocusHere();
			m_assistant_refocus = false;
		}
		ImGui::InputTextMultiline("##prompt", m_assistant_input, sizeof(m_assistant_input), ImVec2(-1.0f, input_height));
		const bool shortcut = ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl &&
			ImGui::IsKeyPressed(ImGuiKey_Enter, false);
		const bool can_send = ready && !pending &&
			std::string(m_assistant_input).find_first_not_of(" \t\r\n") != std::string::npos;

		ImGui::BeginDisabled(!can_send);
		if ((ImGui::Button(TrId("Send")) || shortcut) && can_send)
		{
			const std::string code = i18n::CurrentLanguage();
			if (m_assistant.Send(m_assistant_input, AssistantSelection(), LanguageName(code), code))
			{
				// The field keeps its own copy while it is being edited: let go of it, or the text
				// just sent would come back on the next frame.
				ImGui::ClearActiveID();
				m_assistant_input[0] = '\0';
				m_assistant_refocus = true;
			}
		}
		ImGui::EndDisabled();
		SameLineIfFits(Tr("Ctrl+Enter: send | Enter: new line"));
		ImGui::TextDisabled(Tr("Ctrl+Enter: send | Enter: new line"));
		if (pending)
		{
			if (ImGui::Button(TrId("Cancel request")))
				m_assistant.CancelRequest();
		}
		else if (!ready)
		{
			ImGui::TextWrapped(Tr("Connect a provider to send a request."));
		}
	}

	// ------------------------------------------------------------------------------------------
	// AI Analysis tab
	// ------------------------------------------------------------------------------------------

	void Application::DrawAiAnalysisTab()
	{
		using namespace assistant;
		const uint32_t shader_id = m_selected_shader;
		LoadNotesFor(shader_id);

		const bool ready = m_assistant.IsReady();
		const bool running = m_assistant.IsJobRunning();

		if (!ready)
		{
			ImGui::TextWrapped(Tr("Connect a provider in the AI assistant panel to clean up, classify or explain this "
			                      "shader."));
			if (ImGui::SmallButton(TrId("Open the AI assistant")))
				m_assistant_focus = true;
		}
		else
		{
			ImGui::TextDisabled(Tr("Provider: %s"), m_assistant.ProviderDescription().c_str());
		}

		ImGui::BeginDisabled(!ready || running || m_ai_request.active || shader_id == 0);
		if (ImGui::Button(TrId("Clean up the HLSL")))
			RequestShaderJob(JobKind::cleanup, shader_id);
		ImGui::SameLine();
		if (ImGui::Button(TrId("Classify")))
			RequestShaderJob(JobKind::classification, shader_id);
		ImGui::SameLine();
		if (ImGui::Button(TrId("Explain")))
			RequestShaderJob(JobKind::explanation, shader_id);
		ImGui::EndDisabled();
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", Tr("Each one sends this shader's description, disassembly and decompiled HLSL to the "
		                            "provider. The result is an AI reconstruction, labelled with its model and kept with "
		                            "the shader."));
		ImGui::PopStyleColor();

		if (m_ai_request.active)
			ImGui::TextDisabled(Tr("Waiting for the disassembly and the decompilation..."));
		if (running)
		{
			ImGui::Text(Tr("%s of shader #%u... %llu s"), Tr(JobLabel(m_assistant.RunningJob())), m_assistant.JobShader(),
				static_cast<unsigned long long>(m_assistant.JobSeconds()));
			ImGui::SameLine();
			if (ImGui::SmallButton(TrId("Cancel")))
				m_assistant.CancelJob();
		}
		if (!m_ai_notice.Empty())
			ImGui::TextWrapped("%s", Render(m_ai_notice).c_str());

		ImGui::SeparatorText(Tr("AI results for this shader"));
		if (m_notes_shader != shader_id)
			return;

		int remove = -1;
		bool any = false;
		for (int i = static_cast<int>(m_notes.annotations.size()) - 1; i >= 0; --i)
		{
			const Annotation &annotation = m_notes.annotations[static_cast<size_t>(i)];
			if (annotation.source != AnnotationSource::ai)
				continue;
			any = true;
			ImGui::PushID(i);

			ImGui::TextColored(kAiColour, "%s", annotation.Describe().c_str());
			if (!annotation.created.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", annotation.created.c_str());
			}

			std::string summary;
			std::string hlsl;
			if (annotation.kind == AnnotationKind::cleanup && SplitCleanup(annotation.text, summary, hlsl))
			{
				ImGui::TextWrapped("%s", summary.c_str());
				ImGui::TextColored(kWarningColour, "%s", Tr("AI reconstructed HLSL: never the original source, which "
				                                             "does not exist in a compiled shader."));
				if (ImGui::SmallButton(TrId("Open in the HLSL editor")))
					OpenHlslEditor(shader_id, hlsl, "AI clean-up (" + annotation.author + ")", true);
				ImGui::SameLine();
				if (ImGui::SmallButton(TrId("Copy the HLSL")))
					ImGui::SetClipboardText(hlsl.c_str());
				ImGui::SameLine();
				const bool shown = m_ai_preview_index == i;
				if (ImGui::SmallButton(shown ? TrId("Hide the code") : TrId("Show the code")))
				{
					m_ai_preview_index = shown ? -1 : i;
					if (!shown)
					{
						m_ai_preview.SetText(hlsl);
						m_ai_preview.SetReadOnly(true);
					}
				}
				if (m_ai_preview_index == i)
					m_ai_preview.Render("ai_preview", ImVec2(0.0f, 320.0f));
			}
			else
			{
				ImGui::TextWrapped("%s", annotation.text.c_str());
				if (annotation.kind == AnnotationKind::classification)
				{
					// "Likely role: Bloom (...)": the tag is only applied when a person says so.
					const std::string prefix = "Likely role: ";
					if (annotation.text.compare(0, prefix.size(), prefix) == 0)
					{
						const size_t end = annotation.text.find_first_of(" (\n", prefix.size());
						std::string role = annotation.text.substr(prefix.size(), end == std::string::npos ? std::string::npos : end - prefix.size());
						// Tags such as "Post Process" contain a space: take the longest known name.
						for (uint32_t t = 0; t < static_cast<uint32_t>(ShaderTag::count); ++t)
						{
							const char *name = ShaderTagName(static_cast<ShaderTag>(t));
							if (annotation.text.compare(prefix.size(), std::strlen(name), name) == 0 && std::strlen(name) > role.size())
								role = name;
						}
						ShaderTag tag;
						if (ShaderTagFromName(role, tag) && tag != ShaderTag::unknown && !m_notes.HasTag(tag))
						{
							char button[96];
							std::snprintf(button, sizeof(button), Tr("Apply the tag %s"), role.c_str());
							if (ImGui::SmallButton(button))
							{
								m_notes.SetTag(tag, true);
								SaveNotes();
							}
							ImGui::SameLine();
						}
					}
				}
				if (ImGui::SmallButton(TrId("Copy")))
					ImGui::SetClipboardText(annotation.text.c_str());
			}
			ImGui::SameLine();
			if (ImGui::SmallButton(TrId("Delete")))
				remove = i;
			ImGui::Separator();
			ImGui::PopID();
		}
		if (!any)
			ImGui::TextDisabled(Tr("No AI result for this shader yet."));

		if (remove >= 0)
		{
			m_notes.annotations.erase(m_notes.annotations.begin() + remove);
			SaveNotes();
			m_ai_preview_index = -1;
		}
	}
}
