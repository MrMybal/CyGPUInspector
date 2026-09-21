// CyGPUInspectorApp — the analysis application itself.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#include <CyGPUInspectorCore/Format.hpp>
#include <CyGPUInspectorDecompiler/Disassembler.hpp>
#include <CyGPUInspectorCore/ShaderBlob.hpp>
#include <CyGPUInspectorCore/Version.hpp>

#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder: the default layout is built once, in code

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace cygi
{
	// Every user visible string goes through Tr, which returns the translation when there is one
	// and the English source when there is not. TrId does the same for a label ImGui also uses as
	// an identifier, appending the English as a hidden ### suffix so a layout survives a language
	// change. See CyGPUInspectorCore/Localization.hpp.
	using i18n::Tr;
	using i18n::TrId;
	namespace
	{
		bool ContainsInsensitive(const std::string &haystack, const char *needle)
		{
			if (needle == nullptr || needle[0] == '\0')
				return true;

			std::string lower_haystack = haystack;
			std::string lower_needle = needle;
			auto to_lower = [](std::string &text) {
				std::transform(text.begin(), text.end(), text.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			};
			to_lower(lower_haystack);
			to_lower(lower_needle);
			return lower_haystack.find(lower_needle) != std::string::npos;
		}

		std::string UsageString(uint32_t flags)
		{
			std::string text;
			auto add = [&text](const char *name) {
				if (!text.empty())
					text += " | ";
				text += name;
			};
			if (flags & kUsageBackBuffer) add("BackBuffer");
			if (flags & kUsageRenderTarget) add("RT");
			if (flags & kUsageDepthStencil) add("DSV");
			if (flags & kUsageUnorderedAccess) add("UAV");
			if (flags & kUsageShaderResource) add("SRV");
			if (flags & kUsageConstantBuffer) add("CBV");
			if (flags & kUsageVertexBuffer) add("VB");
			if (flags & kUsageIndexBuffer) add("IB");
			if (flags & kUsageIndirectArgument) add("Indirect");
			if (flags & kUsageCopySource) add("CopySrc");
			if (flags & kUsageCopyDest) add("CopyDst");
			if (flags & kUsageResolveSource) add("ResolveSrc");
			if (flags & kUsageResolveDest) add("ResolveDst");
			if (flags & kUsageShared) add("Shared");
			return text.empty() ? "-" : text;
		}

		void HelpMarker(const char *text)
		{
			ImGui::TextDisabled("(?)");
			if (ImGui::BeginItemTooltip())
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
				ImGui::TextUnformatted(text);
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}
	}

	Application::Application(ID3D11Device *device, ID3D11DeviceContext *context)
		: m_device(device), m_context(context)
	{
		// Language first: everything drawn afterwards asks for translations, and a template dumped
		// from a run has to reflect what was actually shown.
		i18n::ScanLanguages();
		i18n::SetLanguage(LoadPreferredLanguage());

		m_directory.Open();
		RefreshSessions();
		RefreshCaptures();

		// A failure here only costs the channel selection, so it must not stop the application.
		m_preview_renderer_ready = m_preview_renderer.Initialize(device, context);
		m_buffer_writer.Initialize(device, context);

		// Read only by default, as the brief requires: an agent gets to look, not to touch.
		m_mcp.Start();
	}

	void Application::RefreshSessions()
	{
		if (m_directory.IsValid())
			m_sessions = m_directory.List();
	}

	bool Application::ConnectToFirstSession(uint32_t process_id, std::string &message)
	{
		RefreshSessions();

		std::vector<SessionEntry> candidates;
		for (const SessionEntry &entry : m_sessions)
			if (process_id == 0 || entry.process_id == process_id)
				candidates.push_back(entry);

		if (candidates.empty())
		{
			message = process_id == 0
				? "no session is running: start a game with CyGPUInspectorRS, or the fake session"
				: "no session with process id " + std::to_string(process_id);
			return false;
		}
		if (candidates.size() > 1 && process_id == 0)
		{
			// Picking one at random would connect to whichever game happened to start first,
			// which is not a decision this should make on the user's behalf.
			message = std::to_string(candidates.size()) +
				" sessions are running: name one with --connect=<pid>";
			return false;
		}
		if (candidates.size() > 1)
		{
			// One process, several devices: Unreal, for one, creates a device on every adapter
			// while it picks one. The one to look at is the one that presents.
			std::stable_sort(candidates.begin(), candidates.end(), [](const SessionEntry &a, const SessionEntry &b) {
				if (a.IsFresh() != b.IsFresh())
					return a.IsFresh();
				return a.frame_index > b.frame_index;
			});
		}

		ConnectTo(candidates.front());
		message = m_status;
		return m_client != nullptr;
	}

	void Application::ConnectTo(const SessionEntry &entry)
	{
		m_client = std::make_unique<SessionClient>();
		if (!m_client->Connect(entry))
		{
			m_status = "Connection to " + entry.Describe() + " failed: " + m_client->LastError();
			m_client.reset();
			return;
		}

		m_status = "Connected to " + entry.Describe();
		if (!m_client->LastError().empty())
			m_status += " (" + m_client->LastError() + ")";
		m_selected_shader = 0;
		m_selected_resource = 0;
		m_selected_event = 0;
		m_preview.Close();
		m_preview_resource = 0;
		m_preview_message.clear();
		m_analysis.Clear();
		m_disassembly_shader = 0;
		m_disassembly_lines.clear();
		m_graph.Clear();
		m_graph_frame = 0;
		m_selected_pass = 0;
	}

	void Application::StartPreview(uint32_t resource_id)
	{
		if (resource_id == 0)
			return;
		if (!IsLive())
		{
			m_preview_message = "a preview needs a running game: a capture keeps no pixels";
			m_preview_resource = resource_id;
			return;
		}

		m_preview_resource = resource_id;
		m_preview_status = PreviewStatus::ready;
		m_preview_message = "waiting for the game to share the texture...";
		m_preview.Close();

		m_preview_frozen_sent = false;
		m_preview_capture_hold = false;
		m_preview_saved.clear();
		// A new preview starts live; only captures that finish from now on hold its image.
		if (m_client)
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			m_handled_capture_frame = m_client->Model().CaptureState().first_frame;
		}

		// A raw depth buffer looks uniformly white: start such a preview linearised.
		if (resource_id == kPreviewFinalImage)
		{
			if (m_preview_settings.mode == PreviewChannelMode::depth_linear ||
			    m_preview_settings.mode == PreviewChannelMode::depth_raw)
				m_preview_settings.mode = PreviewChannelMode::color;
		}
		else
		{
			SessionModel &model = *ActiveModel();
			std::lock_guard<std::mutex> lock(model.Mutex());
			if (const ResourceInfo *resource = model.ResourceById(resource_id))
			{
				const bool depth = FormatIsDepth(resource->format) ||
					(resource->usage_flags & kUsageDepthStencil) != 0;
				if (depth && m_preview_settings.mode == PreviewChannelMode::color)
					m_preview_settings.mode = PreviewChannelMode::depth_linear;
				else if (!depth && m_preview_settings.mode == PreviewChannelMode::depth_linear)
					m_preview_settings.mode = PreviewChannelMode::color;
			}
		}

		if (!m_client->RequestPreview(resource_id, m_preview_mip, m_preview_slice))
			m_preview_message = "the add-on did not answer the preview request";
	}

	void Application::StopPreview()
	{
		if (m_client)
			m_client->StopPreview();
		m_preview.Close();
		m_preview_resource = 0;
		m_preview_message.clear();
		m_preview_view_this_frame = nullptr;
	}

	void Application::UpdatePreview()
	{
		m_preview_view_this_frame = nullptr;
		if (!IsLive())
		{
			m_preview.Close();
			return;
		}

		// The final image is what someone connecting wants to see first, before knowing which of
		// a few thousand resources is which: shown by itself once per connection. A copy of the
		// presented image per frame, on the GPU, which the game does not notice.
		if (m_preview_auto_client != m_client.get())
		{
			m_preview_auto_client = m_client.get();
			if (m_preview_resource == 0)
				StartPreview(kPreviewFinalImage);
		}

		// Paused timeline, paused image: what is on screen stays the image of the moment being
		// read, instead of the game running on under a frozen timeline.
		// A deep capture that just finished has the add-on hold the image of its last frame.
		{
			CaptureStateRecord capture = {};
			{
				std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
				capture = m_client->Model().CaptureState();
			}
			if (capture.stage == CaptureStage::finished && capture.first_frame != 0 &&
			    capture.first_frame != m_handled_capture_frame && m_preview_resource != 0)
			{
				m_handled_capture_frame = capture.first_frame;
				m_preview_capture_hold = true;
				m_preview_frozen_sent = true;
				m_preview_frozen_frame = capture.first_frame + (capture.frames_done != 0 ? capture.frames_done - 1 : 0);
				// The copy is recorded in that present and executed after it: give it a few
				// interface frames to land before reading it back.
				m_capture_save_countdown = 4;
				m_capture_image_path.clear();
			}
			m_last_capture_stage = capture.stage;
		}

		const bool want_frozen = !m_timeline_follow || m_preview_capture_hold;
		if (m_preview_resource != 0 && m_preview_frozen_sent != want_frozen)
		{
			if (m_client->SetPreviewFrozen(want_frozen))
			{
				m_preview_frozen_sent = want_frozen;
				if (m_preview_frozen_sent)
				{
					std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
					m_preview_frozen_frame = m_client->Model().LastFrame().index;
				}
			}
		}

		PreviewReadyRecord record = {};
		bool has_update = false;
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			has_update = m_client->Model().TakePreviewUpdate(record);
		}
		if (has_update)
			ApplyPreviewUpdate(record);

		// One display pass per interface frame, read by every place that shows the image.
		if (m_preview.IsValid())
		{
			m_preview_view_this_frame = m_preview.View();
			if (m_preview_renderer_ready)
				if (ID3D11ShaderResourceView *rendered = m_preview_renderer.Render(m_preview.View(),
				        m_preview.Width(), m_preview.Height(), m_preview_settings))
					m_preview_view_this_frame = rendered;
		}

		// The image of a finished capture goes to disk by itself: the point of a capture is to be
		// able to look at that moment later, and the pixels are the part that is gone otherwise.
		if (m_capture_save_countdown > 0 && m_preview.IsValid() && --m_capture_save_countdown == 0)
		{
			SavePreviewImage();
			m_capture_image_path = m_preview_saved;
		}
	}

	void Application::ApplyPreviewUpdate(const PreviewReadyRecord &record)
	{
		m_preview_status = record.status;
		if (record.status != PreviewStatus::ready)
		{
			m_preview.Close();
			m_preview_message = PreviewStatusName(record.status);
			return;
		}

		// The handle changes only when the add-on recreated the texture, so reopening here is rare.
		if (m_preview.Open(m_device, record.shared_handle, record.is_nt_handle != 0, record.format,
		                   record.width, record.height))
		{
			m_preview_resource = record.resource_id;
			m_preview_message.clear();
		}
		else
		{
			m_preview_message = "the shared texture could not be opened: " + m_preview.LastError();
		}
	}

	void Application::RequestAnalysis(uint32_t shader_id)
	{
		SessionModel *active = ActiveModel();
		if (active == nullptr || shader_id == 0)
			return;

		// Copy what the worker needs out of the model, then leave the lock before queueing.
		Sha256Digest signature;
		Sha256Digest semantic_hash;
		ShaderStage stage = ShaderStage::unknown;
		ShaderFormat format = ShaderFormat::unknown;
		uint32_t shader_model = 0;
		std::vector<uint8_t> code;
		std::string process_name;
		{
			SessionModel &model = *active;
			std::lock_guard<std::mutex> lock(model.Mutex());

			const ShaderInfo *shader = model.ShaderById(shader_id);
			if (shader == nullptr || shader->code.empty())
				return;

			signature = shader->signature;
			semantic_hash = shader->semantic_hash;
			stage = shader->stage;
			format = shader->format;
			shader_model = shader->shader_model;
			code = shader->code;
			if (model.HasSession())
				process_name = model.Session().process_name;
		}

		m_analysis.Request(shader_id, signature, semantic_hash, stage, format, shader_model,
			std::move(code), std::move(process_name));
	}

	void Application::RefreshDisassemblyLines()
	{
		if (m_selected_shader == m_disassembly_shader)
			return;

		ShaderAnalysis analysis;
		if (!m_analysis.Result(m_selected_shader, analysis) || !analysis.disassembly.ok)
			return;

		// Split once: the viewer renders only the visible lines, but splitting per frame would
		// cost more than the rendering itself on a ten thousand line shader.
		m_disassembly_lines.clear();
		const std::string &text = analysis.disassembly.text;
		size_t start = 0;
		while (start <= text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string::npos)
				end = text.size();

			size_t length = end - start;
			if (length > 0 && text[start + length - 1] == '\r')
				--length;
			m_disassembly_lines.emplace_back(text, start, length);

			if (end == text.size())
				break;
			start = end + 1;
		}
		m_disassembly_shader = m_selected_shader;
	}

	void Application::Draw(double delta_seconds)
	{
		m_since_refresh += delta_seconds;
		if (m_since_refresh > 1.0)
		{
			RefreshSessions();
			m_since_refresh = 0.0;
		}

		UpdatePreview();
		UpdateCaptureBuffers();

		// One MCP tool call is answered per pump, on this thread, with everything it needs at hand.
		m_mcp.Pump([this](const Json &request) { return HandleMcpRequest(request); });

		// Rebuilding a graph of tens of thousands of events every frame would be wasteful; four
		// times a second is well under what the eye notices and keeps the UI thread free.
		m_since_graph += delta_seconds;
		if (m_since_graph > 0.25)
		{
			RefreshFrameGraph(false);
			m_since_graph = 0.0;
		}

		// Fed whatever view is showing, so switching to the continuous one shows the last few
		// seconds instead of starting from an empty axis.
		UpdateFrameTrack();

		m_since_throughput += delta_seconds;
		if (m_client && m_since_throughput > 1.0)
		{
			m_client->SampleThroughput(m_since_throughput);
			m_since_throughput = 0.0;
		}

		const ImGuiViewport *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		constexpr ImGuiWindowFlags kHostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGui::Begin("CyGPUInspectorHost", nullptr, kHostFlags);
		ImGui::PopStyleVar(3);

		DrawMenuBar();

		const ImGuiID dockspace_id = ImGui::GetID("CyGPUInspectorDockspace");
		BuildDefaultLayout(dockspace_id);
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
		ImGui::End();

		// The submission order is the tab order within each dock group, so this follows the layout:
		// what you connect to, then the frame, then the frame's commands, then whatever is selected.
		//
		// Which tab a group *opens on* is a separate matter, and not something the order decides
		// reliably; `SetNextWindowFocus` before a window's `Begin` is what does. It runs for the
		// one frame after the default layout is built, so a fresh install never opens on Status,
		// which is four lines of diagnostics in the widest panel on screen. From then on it is
		// whatever was left selected, which the ini remembers.
		const bool pick_tabs = m_default_tabs_pending > 0;
		if (m_default_tabs_pending > 0)
			--m_default_tabs_pending;

		if (pick_tabs)
			ImGui::SetNextWindowFocus();
		DrawConnectionsPanel();
		DrawCapturesPanel();
		DrawMcpPanel();
		if (pick_tabs)
			ImGui::SetNextWindowFocus();
		DrawShadersPanel();
		DrawResourcesPanel();
		if (pick_tabs)
			ImGui::SetNextWindowFocus();
		DrawTimelinePanel();
		DrawFrameGraphPanel();
		DrawPreviewPanel();
		if (pick_tabs)
			ImGui::SetNextWindowFocus();
		DrawEventsPanel();
		DrawDeepCapturePanel();
		DrawStatusBar();
		DrawDetailsPanel();
		if (pick_tabs)
			ImGui::SetNextWindowFocus();
		DrawShaderCodePanel();
		DrawModExportPanel();

		if (m_show_demo)
			ImGui::ShowDemoWindow(&m_show_demo);
		if (m_show_about)
			DrawAboutWindow();
	}

	// A first run used to open on a dozen panels stacked in the top left corner, which is not a
	// layout, it is a pile. This builds one the first time — and only when the saved settings have
	// none, so it never overwrites an arrangement someone has made.
	//
	// The names passed here are the English ones on purpose: ImGui hashes a window by the part
	// after `###`, and TrId keeps the English there, so one layout serves every language.
	void Application::BuildDefaultLayout(unsigned int dockspace_id)
	{
		if (m_layout_built)
			return;
		m_layout_built = true;

		// Someone's own layout, restored from the ini: leave it completely alone — unless they
		// asked for the default one back, which is also the way back for a panel dragged out of
		// the window and lost.
		const bool reset = m_reset_layout;
		m_reset_layout = false;
		if (reset)
			ImGui::DockBuilderRemoveNode(dockspace_id);
		else if (ImGui::DockBuilderGetNode(dockspace_id) != nullptr)
			return;

		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);

		// Splitting a node turns it into a parent, and docking into a parent does not do what it
		// looks like it does. Every split therefore keeps *both* halves and only leaves are used.
		ImGuiID centre = dockspace_id;
		ImGuiID left = 0, right = 0, bottom = 0;
		left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.22f, nullptr, &centre);
		right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.30f, nullptr, &centre);
		bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.42f, nullptr, &centre);

		ImGuiID left_top = 0, right_top = 0;
		const ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.62f, nullptr, &left_top);
		const ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right_top);

		// Left: what you are connected to, and what to pick from.
		ImGui::DockBuilderDockWindow("Connections", left_top);
		ImGui::DockBuilderDockWindow("Captures", left_top);
		ImGui::DockBuilderDockWindow("MCP", left_top);
		ImGui::DockBuilderDockWindow("Shaders", left_bottom);
		ImGui::DockBuilderDockWindow("Resources", left_bottom);

		// Centre: the frame, which is what the tool is for.
		ImGui::DockBuilderDockWindow("Frame timeline", centre);
		ImGui::DockBuilderDockWindow("Frame graph", centre);
		ImGui::DockBuilderDockWindow("Preview", centre);

		// Bottom centre: the commands of that frame, and the deep capture of one of them.
		ImGui::DockBuilderDockWindow("Frame events", bottom);
		ImGui::DockBuilderDockWindow("Deep capture", bottom);
		ImGui::DockBuilderDockWindow("Status", bottom);

		// Right: everything about whatever is selected.
		ImGui::DockBuilderDockWindow("Details", right_top);
		ImGui::DockBuilderDockWindow("Shader code", right_bottom);
		ImGui::DockBuilderDockWindow("Mod export", right_bottom);

		ImGui::DockBuilderFinish(dockspace_id);
		m_default_tabs_pending = 3;
	}

	bool Application::SetTrackingLevel(TrackingLevel level, std::string &message)
	{
		if (!m_client)
		{
			message = "not connected to a session";
			return false;
		}
		if (!m_client->SetLevel(level))
		{
			message = "the session refused the tracking level";
			return false;
		}
		message = std::string("tracking level set to ") + TrackingLevelName(level);
		return true;
	}

	void Application::DrawMenuBar()
	{
		if (!ImGui::BeginMenuBar())
			return;

		// The logo, at the height of the menu text, before the first menu.
		if (m_logo != nullptr)
		{
			const float size = ImGui::GetFontSize() + 2.0f;
			ImGui::Image(reinterpret_cast<ImTextureID>(m_logo), ImVec2(size, size));
			if (ImGui::IsItemClicked())
				m_show_about = true;
		}

		if (ImGui::BeginMenu(TrId("File")))
		{
			if (ImGui::MenuItem(TrId("Refresh connections")))
				RefreshSessions();
			if (ImGui::MenuItem(TrId("Save frame to disk"), nullptr, false, m_client != nullptr))
				SaveCapture();
			if (ImGui::MenuItem(TrId("Deep capture now"), nullptr, false, IsLive()))
				StartDeepCapture();
			if (ImGui::MenuItem(TrId("Close capture"), nullptr, false, m_capture_model != nullptr))
				CloseCapture();
			if (ImGui::MenuItem(TrId("Disconnect"), nullptr, false, m_client != nullptr))
			{
				m_client.reset();
				m_status = "Disconnected";
			}
			ImGui::Separator();
			if (ImGui::MenuItem(TrId("Exit")))
				m_wants_exit = true;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(TrId("View")))
		{
			if (ImGui::BeginMenu(TrId("Language")))
			{
				for (const i18n::LanguageInfo &language : i18n::Languages())
				{
					const bool active = language.code == i18n::CurrentLanguage();
					if (ImGui::MenuItem(language.name.c_str(), language.code.c_str(), active) && !active)
					{
						i18n::SetLanguage(language.code);
						SavePreferredLanguage(language.code);
					}
					if (!language.path.empty() && ImGui::BeginItemTooltip())
					{
						ImGui::Text(Tr("%u string(s) translated"), language.translated);
						ImGui::TextDisabled("%s", language.path.c_str());
						ImGui::EndTooltip();
					}
				}

				ImGui::Separator();
				ImGui::TextDisabled(Tr("%u string(s) shown so far, %u without a translation"),
					i18n::SeenCount(), i18n::MissingCount());
				// The template is written from a real run, so a translator gets a file that matches
				// the build in front of them rather than one that matches someone's memory.
				if (ImGui::MenuItem(TrId("Write translation template...")))
					WriteTranslationTemplate();
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem(TrId("Reset the layout")))
			{
				m_reset_layout = true;
				m_layout_built = false;
			}
			ImGui::MenuItem(TrId("ImGui demo window"), nullptr, &m_show_demo);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(TrId("Help")))
		{
			if (ImGui::MenuItem(TrId("About CyGPUInspector")))
				m_show_about = true;
			ImGui::Separator();
			ImGui::MenuItem("CyGPUInspector " CYGI_VERSION_STRING, nullptr, false, false);
			ImGui::MenuItem(TrId("GNU AGPL v3 or later"), nullptr, false, false);
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void Application::DrawAboutWindow()
	{
		const ImGuiViewport *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
			viewport->WorkPos.y + viewport->WorkSize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		if (!ImGui::Begin(TrId("About CyGPUInspector"), &m_show_about,
		                  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
		{
			ImGui::End();
			return;
		}

		if (m_logo != nullptr)
		{
			ImGui::Image(reinterpret_cast<ImTextureID>(m_logo), ImVec2(128.0f, 128.0f));
			ImGui::SameLine();
		}
		ImGui::BeginGroup();
		ImGui::Text("CyGPUInspector %s", CYGI_VERSION_STRING);
		ImGui::TextDisabled("%s", Tr("GPU frame, shader and resource inspector"));
		ImGui::Spacing();
		ImGui::TextUnformatted(Tr("Captures through the official ReShade add-on API."));
		ImGui::TextUnformatted(Tr("Nothing is injected, hooked or hidden by this tool itself."));
		ImGui::Spacing();
		ImGui::TextUnformatted(Tr("Copyright (C) 2026 Cyberalien"));
		ImGui::TextUnformatted(Tr("GNU AGPL v3 or later: see LICENSE and THIRD-PARTY.md."));
		ImGui::EndGroup();
		ImGui::End();
	}

	void Application::DrawConnectionsPanel()
	{
		if (!ImGui::Begin(TrId("Connections")))
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted(Tr("Applications running CyGPUInspectorRS"));
		ImGui::SameLine();
		HelpMarker(Tr("Every ReShade add-on instance publishes itself in a shared table. A session "
		              "disappears from this list when its process exits."));
		ImGui::Separator();

		if (m_sessions.empty())
		{
			ImGui::TextDisabled(Tr("No application connected."));
			ImGui::TextWrapped(Tr("Copy CyGPUInspectorRS.addon64 next to a game that runs ReShade "
			                      "(add-on enabled build) and start it."));
		}

		for (const SessionEntry &entry : m_sessions)
		{
			ImGui::PushID(static_cast<int>(entry.process_id * 16 + entry.device_index));

			const bool is_current = m_client && m_client->Entry().process_id == entry.process_id &&
			                        m_client->Entry().device_index == entry.device_index;

			ImGui::BulletText("%s", entry.Describe().c_str());
			ImGui::Indent();
			ImGui::TextDisabled(Tr("frame %llu, %s, %s"), static_cast<unsigned long long>(entry.frame_index),
				TrackingLevelName(entry.level), entry.IsFresh() ? "alive" : "no heartbeat");

			if (is_current)
			{
				ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), Tr("connected"));
				ImGui::SameLine();
				if (ImGui::SmallButton(TrId("Disconnect")))
				{
					m_client.reset();
					m_status = "Disconnected";
				}
			}
			else if (ImGui::SmallButton(TrId("Connect")))
			{
				ConnectTo(entry);
			}
			ImGui::Unindent();
			ImGui::PopID();
		}

		if (m_client)
		{
			ImGui::Separator();
			SessionModel &model = *ActiveModel();
			// The pipe commands below must not run while the model lock is held: a command waits
			// for the game's present thread, which would stall the reader thread for a frame.
			int requested_level = -1;
			{
			std::lock_guard<std::mutex> lock(model.Mutex());

			if (model.HasSession())
			{
				const SessionInfoRecord &session = model.Session();
				ImGui::Text(Tr("Add-on:   %s"), session.addon_version);
				ImGui::Text(Tr("Adapter:  %s"), session.adapter_name);
				ImGui::Text(Tr("Caps:     %s%s%s%s"),
					(session.capability_flags & kCapSharedResource) ? "shared " : "",
					(session.capability_flags & kCapSharedResourceNtHandle) ? "shared-nt " : "",
					(session.capability_flags & kCapSharedFence) ? "fence " : "",
					(session.capability_flags & kCapTimestampQueries) ? "timestamps" : "");

				int level_index = static_cast<int>(session.level);
				static const char *const kLevels[] = { "Idle", "Tracking", "Pass Timing", "Capture",
					"Full Draw Timing" };
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::Combo(TrId("Tracking level"), &level_index, kLevels, IM_ARRAYSIZE(kLevels)))
					requested_level = level_index;
			}
			}

			if (requested_level >= 0)
				m_client->SetLevel(static_cast<TrackingLevel>(requested_level));

			if (ImGui::Button(TrId("Resend everything")))
				m_client->RequestFullSync();
		}

		ImGui::End();
	}

	void Application::DrawShadersPanel()
	{
		if (!ImGui::Begin(TrId("Shaders")))
		{
			ImGui::End();
			return;
		}

		if (ActiveModel() == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		SessionModel &model = *ActiveModel();
		std::lock_guard<std::mutex> lock(model.Mutex());

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##shaderfilter", Tr("filter by signature or stage"), m_shader_filter,
			sizeof(m_shader_filter));
		ImGui::SameLine();
		ImGui::Checkbox(TrId("Used this frame"), &m_only_used_this_frame);
		ImGui::SameLine();
		ImGui::Text(Tr("%zu shaders"), model.Shaders().size());

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
			ImGuiTableFlags_Hideable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

		if (ImGui::BeginTable("shaders", 8, kFlags))
		{
			// Eight columns in a side panel means eight clipped columns. Bytes, the shader model
			// and the signature are read in Details, where there is room; what stays here is what
			// you scan and sort a list by. They are only hidden by default: the table's own
			// context menu still brings any of them back.
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(TrId("Id"), ImGuiTableColumnFlags_WidthFixed |
				ImGuiTableColumnFlags_DefaultSort, 34.0f);
			ImGui::TableSetupColumn(TrId("Stage"), ImGuiTableColumnFlags_WidthFixed, 96.0f);
			ImGui::TableSetupColumn(TrId("Format"), ImGuiTableColumnFlags_WidthFixed, 56.0f);
			ImGui::TableSetupColumn(TrId("SM"), ImGuiTableColumnFlags_DefaultHide);
			ImGui::TableSetupColumn(TrId("Bytes"), ImGuiTableColumnFlags_DefaultHide);
			ImGui::TableSetupColumn(TrId("Draws/frame"), ImGuiTableColumnFlags_WidthFixed |
				ImGuiTableColumnFlags_PreferSortDescending, 82.0f);
			ImGui::TableSetupColumn(TrId("GPU ms"), ImGuiTableColumnFlags_WidthFixed |
				ImGuiTableColumnFlags_PreferSortDescending, 62.0f);
			ImGui::TableSetupColumn(TrId("Signature"),
				ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultHide);
			ImGui::TableHeadersRow();

			// Filtered first, then sorted by whichever column was clicked, then drawn through a
			// clipper: a game has thousands of shaders, and "which one costs the most" is a sort.
			std::vector<const ShaderInfo *> rows;
			rows.reserve(model.Shaders().size());
			for (const ShaderInfo &shader : model.Shaders())
			{
				if (shader.id == 0)
					continue;
				if (m_only_used_this_frame && shader.draws_this_frame == 0 && shader.dispatches_this_frame == 0)
					continue;

				const std::string searchable = std::string(ShaderStageName(shader.stage)) + " " +
					shader.signature.ToHex();
				if (!ContainsInsensitive(searchable, m_shader_filter))
					continue;
				rows.push_back(&shader);
			}

			if (const ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0)
			{
				const ImGuiTableColumnSortSpecs spec = specs->Specs[0];
				const bool ascending = spec.SortDirection != ImGuiSortDirection_Descending;
				std::stable_sort(rows.begin(), rows.end(), [&](const ShaderInfo *a, const ShaderInfo *b) {
					int order = 0;
					switch (spec.ColumnIndex)
					{
					case 1: order = static_cast<int>(a->stage) - static_cast<int>(b->stage); break;
					case 2: order = static_cast<int>(a->format) - static_cast<int>(b->format); break;
					case 3: order = static_cast<int>(a->shader_model) - static_cast<int>(b->shader_model); break;
					case 4: order = a->code_size < b->code_size ? -1 : (a->code_size > b->code_size ? 1 : 0); break;
					case 5:
					{
						const uint32_t left = a->draws_this_frame + a->dispatches_this_frame;
						const uint32_t right = b->draws_this_frame + b->dispatches_this_frame;
						order = left < right ? -1 : (left > right ? 1 : 0);
						break;
					}
					case 6: order = a->gpu_ticks < b->gpu_ticks ? -1 : (a->gpu_ticks > b->gpu_ticks ? 1 : 0); break;
					case 7: order = a->signature.ToHex().compare(b->signature.ToHex()); break;
					default: order = a->id < b->id ? -1 : (a->id > b->id ? 1 : 0); break;
					}
					if (order == 0)
						return a->id < b->id;
					return ascending ? order < 0 : order > 0;
				});
			}

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step())
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const ShaderInfo &shader = *rows[static_cast<size_t>(row)];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				char label[32];
				std::snprintf(label, sizeof(label), "%u", shader.id);
				if (ImGui::Selectable(label, m_selected_shader == shader.id, ImGuiSelectableFlags_SpanAllColumns))
					m_selected_shader = shader.id;

				ImGui::TableNextColumn();
				if (shader.disabled)
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), Tr("%s (off)"), ShaderStageName(shader.stage));
				else
					ImGui::TextUnformatted(ShaderStageName(shader.stage));

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(ShaderFormatName(shader.format));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(ShaderModelName(shader.shader_model));
				ImGui::TableNextColumn();
				ImGui::Text("%u", shader.code_size);
				ImGui::TableNextColumn();
				ImGui::Text("%u", shader.draws_this_frame + shader.dispatches_this_frame);
				ImGui::TableNextColumn();
				if (shader.gpu_ticks != 0 && model.Timings().IsValid())
					ImGui::Text("%.3f", model.Timings().Milliseconds(shader.gpu_ticks));
				else
					ImGui::TextDisabled("-");
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(shader.ShortSignature().c_str());
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void Application::DrawResourcesPanel()
	{
		if (!ImGui::Begin(TrId("Resources")))
		{
			ImGui::End();
			return;
		}

		if (ActiveModel() == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		SessionModel &model = *ActiveModel();
		// A preview request talks to the game and must not run while the model lock is held.
		uint32_t preview_request = 0;
		{
		std::lock_guard<std::mutex> lock(model.Mutex());

		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##resourcefilter", Tr("filter by format or kind"), m_resource_filter,
			sizeof(m_resource_filter));
		ImGui::SameLine();
		ImGui::Checkbox(TrId("Render targets only"), &m_only_render_targets);
		ImGui::SameLine();
		ImGui::Text(Tr("%zu resources"), model.Resources().size());

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
			ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit;

		if (ImGui::BeginTable("resources", 8, kFlags))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(TrId("Id"), ImGuiTableColumnFlags_DefaultSort);
			ImGui::TableSetupColumn(TrId("Name"));
			ImGui::TableSetupColumn(TrId("Kind"));
			ImGui::TableSetupColumn(TrId("Size"), ImGuiTableColumnFlags_PreferSortDescending);
			ImGui::TableSetupColumn(TrId("Format"));
			ImGui::TableSetupColumn(TrId("Mips"), ImGuiTableColumnFlags_PreferSortDescending);
			ImGui::TableSetupColumn(TrId("W/frame"), ImGuiTableColumnFlags_PreferSortDescending);
			ImGui::TableSetupColumn(TrId("Usage"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			std::vector<const ResourceInfo *> rows;
			rows.reserve(model.Resources().size());
			for (const ResourceInfo &resource : model.Resources())
			{
				if (resource.id == 0)
					continue;
				if (m_only_render_targets &&
				    (resource.usage_flags & (kUsageRenderTarget | kUsageDepthStencil | kUsageUnorderedAccess)) == 0)
					continue;

				const std::string searchable = std::string(ResourceKindName(resource.kind)) + " " +
					FormatName(resource.format) + " " + resource.name;
				if (!ContainsInsensitive(searchable, m_resource_filter))
					continue;
				rows.push_back(&resource);
			}

			if (const ImGuiTableSortSpecs *specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0)
			{
				const ImGuiTableColumnSortSpecs spec = specs->Specs[0];
				const bool ascending = spec.SortDirection != ImGuiSortDirection_Descending;
				// A texture and a buffer are compared by what they occupy, which is the question a
				// size column is sorted to answer.
				const auto bytes = [](const ResourceInfo &resource) -> uint64_t {
					if (resource.kind == ResourceKind::buffer)
						return resource.buffer_size;
					const uint32_t texel = FormatBytesPerPixel(resource.format);
					return static_cast<uint64_t>(resource.width) * resource.height *
						std::max<uint32_t>(resource.depth_or_layers, 1) * (texel != 0 ? texel : 4);
				};
				std::stable_sort(rows.begin(), rows.end(), [&](const ResourceInfo *a, const ResourceInfo *b) {
					int order = 0;
					switch (spec.ColumnIndex)
					{
					case 1:
						// Named first, whichever way it is sorted: the unnamed are the ones to skip.
						if (a->name.empty() != b->name.empty())
							return !a->name.empty();
						order = a->name.compare(b->name);
						break;
					case 2: order = static_cast<int>(a->kind) - static_cast<int>(b->kind); break;
					case 3:
					{
						const uint64_t left = bytes(*a);
						const uint64_t right = bytes(*b);
						order = left < right ? -1 : (left > right ? 1 : 0);
						break;
					}
					case 4: order = std::strcmp(FormatName(a->format), FormatName(b->format)); break;
					case 5: order = static_cast<int>(a->mip_levels) - static_cast<int>(b->mip_levels); break;
					case 6:
						order = a->writes_this_frame < b->writes_this_frame ? -1
							: (a->writes_this_frame > b->writes_this_frame ? 1 : 0);
						break;
					case 7: order = a->usage_flags < b->usage_flags ? -1 : (a->usage_flags > b->usage_flags ? 1 : 0); break;
					default: order = a->id < b->id ? -1 : (a->id > b->id ? 1 : 0); break;
					}
					if (order == 0)
						return a->id < b->id;
					return ascending ? order < 0 : order > 0;
				});
			}

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.size()));
			while (clipper.Step())
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const ResourceInfo &resource = *rows[static_cast<size_t>(row)];
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				char label[32];
				std::snprintf(label, sizeof(label), "%u", resource.id);
				if (ImGui::Selectable(label, m_selected_resource == resource.id,
				                      ImGuiSelectableFlags_SpanAllColumns |
				                      ImGuiSelectableFlags_AllowDoubleClick))
				{
					m_selected_resource = resource.id;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
						preview_request = resource.id;
				}

				ImGui::TableNextColumn();
				if (resource.name.empty())
					ImGui::TextDisabled("-");
				else
					ImGui::TextUnformatted(resource.name.c_str());

				ImGui::TableNextColumn();
				if (resource.alive)
					ImGui::TextUnformatted(ResourceKindName(resource.kind));
				else
					ImGui::TextDisabled(Tr("%s (gone)"), ResourceKindName(resource.kind));

				ImGui::TableNextColumn();
				if (resource.kind == ResourceKind::buffer)
					ImGui::Text(Tr("%llu B"), static_cast<unsigned long long>(resource.buffer_size));
				else
					ImGui::Text(Tr("%ux%u"), resource.width, resource.height);

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(resource.kind == ResourceKind::buffer ? "-" : FormatName(resource.format));
				ImGui::TableNextColumn();
				ImGui::Text("%u", resource.mip_levels);
				ImGui::TableNextColumn();
				ImGui::Text("%u", resource.writes_this_frame);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(UsageString(resource.usage_flags).c_str());
			}
			ImGui::EndTable();
		}
		}

		if (preview_request != 0)
			StartPreview(preview_request);

		ImGui::End();
	}

	void Application::SelectEvent(uint32_t index)
	{
		m_selected_event = index;
		m_events_scroll_pending = true;
	}

	void Application::DrawEventsPanel()
	{
		if (!ImGui::Begin(TrId("Frame events")))
		{
			ImGui::End();
			return;
		}

		if (ActiveModel() == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		SessionModel &model = *ActiveModel();
		std::lock_guard<std::mutex> lock(model.Mutex());
		const FrameInfo &frame = model.LastFrame();

		ImGui::Text(Tr("Frame %llu: %u draws, %u dispatches, %zu events"),
			static_cast<unsigned long long>(frame.index), frame.draw_count, frame.dispatch_count,
			frame.events.size());
		if (frame.dropped_events != 0)
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), Tr("%u events dropped in the game process"),
				frame.dropped_events);

		if (model.Timings().IsValid())
		{
			ImGui::SameLine();
			ImGui::TextDisabled(Tr("| GPU timings from frame %llu, %.2f ms total"),
				static_cast<unsigned long long>(model.Timings().frame_index),
				model.Timings().Milliseconds(model.Timings().total_ticks));
			ImGui::SameLine();
			HelpMarker(Tr("ReShade calls an add-on before a command, never after, so the time shown "
			              "for a command is the interval from its start to the next recorded command. "
			              "Results come back three frames late, which is why they carry their own "
			              "frame number."));
		}

		std::vector<uint32_t> shader_events;
		if (m_selected_shader != 0)
		{
			shader_events = model.EventsUsingShader(m_selected_shader);
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), Tr("| shader #%u: %zu events"), m_selected_shader,
				shader_events.size());
		}

		// The three filters that answer an actual question: what did the frame draw, where does
		// this shader get used, and what did this pass do. Each is a row set, not a highlight, so
		// scrolling through the answer does not mean scrolling past everything else.
		ImGui::Checkbox(TrId("Draws only"), &m_events_draws_only);
		ImGui::SameLine();
		ImGui::BeginDisabled(m_selected_shader == 0);
		ImGui::Checkbox(TrId("Selected shader"), &m_events_shader_only);
		ImGui::EndDisabled();
		ImGui::SameLine();
		const GraphPass *selected_pass = m_graph.PassById(m_selected_pass);
		ImGui::BeginDisabled(selected_pass == nullptr);
		ImGui::Checkbox(TrId("Selected pass"), &m_events_pass_only);
		ImGui::EndDisabled();

		const bool by_shader = m_events_shader_only && m_selected_shader != 0;
		const bool by_pass = m_events_pass_only && selected_pass != nullptr;
		const bool filtering = m_events_draws_only || by_shader || by_pass;

		// Row numbers rather than a copy of the events: a frame can be a hundred thousand of them.
		std::vector<int> rows;
		if (filtering)
		{
			rows.reserve(frame.events.size());
			for (size_t i = 0; i < frame.events.size(); ++i)
			{
				const FrameEvent &event = frame.events[i];
				if (m_events_draws_only && !IsDrawOrDispatch(event.kind))
					continue;
				if (by_pass && (event.index < selected_pass->first_event ||
				                event.index > selected_pass->last_event))
					continue;
				if (by_shader && !std::binary_search(shader_events.begin(), shader_events.end(),
				                                    event.index))
					continue;
				rows.push_back(static_cast<int>(i));
			}
			ImGui::SameLine();
			ImGui::TextDisabled(Tr("| %zu of %zu shown"), rows.size(), frame.events.size());
		}

		constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable |
			ImGuiTableFlags_SizingFixedFit;

		if (ImGui::BeginTable("events", 8, kFlags))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 52.0f);
			ImGui::TableSetupColumn(TrId("Kind"));
			ImGui::TableSetupColumn(TrId("Pipeline"));
			ImGui::TableSetupColumn(TrId("Target"));
			ImGui::TableSetupColumn(TrId("Depth"));
			ImGui::TableSetupColumn(TrId("Counts"));
			ImGui::TableSetupColumn(TrId("GPU ms"));
			ImGui::TableSetupColumn(TrId("Flags"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			// Tens of thousands of rows are normal: only build the visible ones.
			const int shown = filtering ? static_cast<int>(rows.size())
			                            : static_cast<int>(frame.events.size());
			const auto event_of_row = [&](int row) -> const FrameEvent & {
				return frame.events[static_cast<size_t>(filtering ? rows[static_cast<size_t>(row)]
				                                                  : row)];
			};

			ImGuiListClipper clipper;
			clipper.Begin(shown);

			// The row of the command selected elsewhere. It is not the command's own number: a
			// frame can start part way through if the ring buffer dropped its beginning, and a
			// filter renumbers everything anyway. The clipper has to be told to build that row,
			// or there is nothing to scroll to.
			int scroll_to = -1;
			if (m_events_scroll_pending)
			{
				for (int row = 0; row < shown; ++row)
					if (event_of_row(row).index == m_selected_event)
					{
						scroll_to = row;
						clipper.IncludeItemByIndex(row);
						break;
					}
				if (scroll_to < 0)
					m_events_scroll_pending = false;
			}

			while (clipper.Step())
			{
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
				{
					const FrameEvent &event = event_of_row(row);

					ImGui::TableNextRow();
					if (row == scroll_to)
					{
						// A third of the way down, so the commands before it are visible too.
						ImGui::SetScrollHereY(0.33f);
						m_events_scroll_pending = false;
					}
					if (m_selected_shader != 0 &&
					    std::binary_search(shader_events.begin(), shader_events.end(), event.index))
						ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.1f, 0.3f, 0.5f, 0.5f)));

					ImGui::TableNextColumn();
					char label[32];
					std::snprintf(label, sizeof(label), "%u", event.index);
					if (ImGui::Selectable(label, m_selected_event == event.index,
					                      ImGuiSelectableFlags_SpanAllColumns))
					{
						m_selected_event = event.index;
						m_events_scroll_pending = false;
						if (event.pipeline_id != 0)
						{
							const std::vector<uint32_t> ids = model.ShadersOfPipeline(event.pipeline_id);
							if (!ids.empty())
								m_selected_shader = ids.front();
						}
						if (event.primary_resource != 0)
							m_selected_resource = event.primary_resource;
					}

					ImGui::TableNextColumn();
					ImGui::TextUnformatted(EventKindName(event.kind));
					ImGui::TableNextColumn();
					if (event.pipeline_id != 0)
						ImGui::Text("%u", event.pipeline_id);
					else
						ImGui::TextDisabled("-");
					ImGui::TableNextColumn();
					if (event.primary_resource != 0)
						ImGui::Text(Tr("#%u"), event.primary_resource);
					else
						ImGui::TextDisabled("-");
					ImGui::TableNextColumn();
					if (event.secondary_resource != 0)
						ImGui::Text(Tr("#%u"), event.secondary_resource);
					else
						ImGui::TextDisabled("-");
					ImGui::TableNextColumn();
					ImGui::Text(Tr("%u/%u"), event.a, event.b);
					ImGui::TableNextColumn();
					{
						const auto timing = model.Timings().by_event.find(event.index);
						if (timing != model.Timings().by_event.end() && model.Timings().IsValid())
							ImGui::Text("%.3f", model.Timings().Milliseconds(timing->second));
						else
							ImGui::TextDisabled("-");
					}
					ImGui::TableNextColumn();
					if (event.flags & kEventReplaced)
						ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), Tr("replaced"));
					else if (event.flags & kEventSkipped)
						ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), Tr("skipped"));
					else if (event.flags & kEventIndirect)
						ImGui::TextDisabled(Tr("indirect"));
					else
						ImGui::TextDisabled("-");
				}
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void Application::DrawPreviewPanel()
	{
		if (!ImGui::Begin(TrId("Preview")))
		{
			ImGui::End();
			return;
		}

		if (ActiveModel() == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		if (m_preview_resource == 0)
		{
			if (IsLive() && ImGui::Button(TrId("Show the final image")))
				StartPreview(kPreviewFinalImage);
			ImGui::TextDisabled(Tr("No resource previewed."));
			ImGui::TextWrapped(Tr("Select a resource and press \"Preview\" in the Details panel, or "
			                      "double click a row in the Resources panel."));
			ImGui::End();
			return;
		}

		if (m_preview_resource == kPreviewFinalImage)
			ImGui::TextUnformatted(Tr("Final image"));
		else
			ImGui::Text(Tr("Resource #%u"), m_preview_resource);
		ImGui::SameLine();
		if (m_preview_resource != kPreviewFinalImage && IsLive() && ImGui::SmallButton(TrId("Final image")))
			StartPreview(kPreviewFinalImage);
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Save as PNG")))
			SavePreviewImage();
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Stop")))
		{
			StopPreview();
			ImGui::End();
			return;
		}
		if (m_preview_frozen_sent)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), Tr("frozen at frame %llu while Follow is off"),
				static_cast<unsigned long long>(m_preview_frozen_frame));
		}
		if (!m_preview_saved.empty())
			ImGui::TextDisabled("%s", m_preview_saved.c_str());

		// Mip and slice are part of the request: changing them asks the add-on for another copy.
		bool reselect = false;
		int mip = static_cast<int>(m_preview_mip);
		int slice = static_cast<int>(m_preview_slice);
		uint32_t mip_count = 1;
		uint32_t slice_count = 1;
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			if (const ResourceInfo *resource = m_client->Model().ResourceById(m_preview_resource))
			{
				mip_count = resource->mip_levels != 0 ? resource->mip_levels : 1;
				slice_count = resource->depth_or_layers != 0 ? resource->depth_or_layers : 1;
			}
		}

		if (mip_count > 1)
		{
			ImGui::SetNextItemWidth(160.0f);
			if (ImGui::SliderInt(TrId("Mip"), &mip, 0, static_cast<int>(mip_count) - 1))
				reselect = true;
		}
		if (slice_count > 1)
		{
			ImGui::SetNextItemWidth(160.0f);
			if (ImGui::SliderInt(TrId("Slice"), &slice, 0, static_cast<int>(slice_count) - 1))
				reselect = true;
		}
		if (reselect)
		{
			m_preview_mip = static_cast<uint32_t>(mip);
			m_preview_slice = static_cast<uint32_t>(slice);
			StartPreview(m_preview_resource);
		}

		ImGui::Checkbox(TrId("Fit"), &m_preview_fit);
		if (!m_preview_fit)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(160.0f);
			ImGui::SliderFloat(TrId("Zoom"), &m_preview_zoom, 0.1f, 8.0f, "%.2fx");
		}

		// Channel selection and depth linearisation, applied by our own display pass.
		if (m_preview_renderer_ready)
		{
			static const char *const kModes[] = { "RGB", "R", "G", "B", "A", "Depth (raw)",
				"Depth (linearised)" };
			int mode = static_cast<int>(m_preview_settings.mode);
			ImGui::SetNextItemWidth(170.0f);
			if (ImGui::Combo(TrId("Channel"), &mode, kModes, IM_ARRAYSIZE(kModes)))
				m_preview_settings.mode = static_cast<PreviewChannelMode>(mode);

			ImGui::SetNextItemWidth(220.0f);
			ImGui::DragFloatRange2("Range", &m_preview_settings.range_min, &m_preview_settings.range_max,
				0.01f, -16.0f, 16.0f, "%.3f", "%.3f");
			ImGui::SameLine();
			if (ImGui::SmallButton("0..1"))
			{
				m_preview_settings.range_min = 0.0f;
				m_preview_settings.range_max = 1.0f;
			}

			if (m_preview_settings.mode == PreviewChannelMode::depth_linear)
			{
				ImGui::SetNextItemWidth(90.0f);
				ImGui::DragFloat(TrId("Near"), &m_preview_settings.near_plane, 0.01f, 0.001f, 100.0f);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90.0f);
				ImGui::DragFloat(TrId("Far"), &m_preview_settings.far_plane, 1.0f, 1.0f, 100000.0f);
				ImGui::SameLine();
				ImGui::Checkbox(TrId("Reverse-Z"), &m_preview_settings.reverse_z);
			}
		}
		else
		{
			ImGui::TextDisabled(Tr("channel selection unavailable: %s"),
				m_preview_renderer.LastError().c_str());
		}

		if (!m_preview_message.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", m_preview_message.c_str());

		// A shared texture only opens on the adapter that created it. Said here, with both names,
		// rather than left as an opening error nobody can act on.
		if (m_client && !m_adapter_name.empty())
		{
			std::string game_adapter;
			{
				std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
				if (m_client->Model().HasSession())
					game_adapter = m_client->Model().Session().adapter_name;
			}
			if (!game_adapter.empty() && game_adapter.find(m_adapter_name) == std::string::npos &&
			    m_adapter_name.find(game_adapter) == std::string::npos)
				ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f),
					Tr("The game renders on %s and this application on %s: an image can only be shared on the "
					   "same graphics card."), game_adapter.c_str(), m_adapter_name.c_str());
		}

		if (!m_preview.IsValid())
		{
			ImGui::End();
			return;
		}

		ImGui::Text(Tr("%u x %u  %s"), m_preview.Width(), m_preview.Height(), FormatName(m_preview.Format()));
		ImGui::Separator();

		const float texture_width = static_cast<float>(m_preview.Width());
		const float texture_height = static_cast<float>(m_preview.Height());
		ImVec2 size(texture_width, texture_height);

		if (m_preview_fit)
		{
			const ImVec2 available = ImGui::GetContentRegionAvail();
			const float scale = (available.x > 0.0f && available.y > 0.0f)
				? (std::min)(available.x / texture_width, available.y / texture_height) : 1.0f;
			size = ImVec2(texture_width * scale, texture_height * scale);
		}
		else
		{
			size = ImVec2(texture_width * m_preview_zoom, texture_height * m_preview_zoom);
		}

		// The shared texture lives in the game's GPU memory. The display pass reads it there and
		// writes a viewable copy, once per interface frame (UpdatePreview); nothing ever travels
		// through system memory unless it is saved.
		ID3D11ShaderResourceView *view = m_preview_view_this_frame != nullptr ? m_preview_view_this_frame
		                                                                      : m_preview.View();
		ImGui::Image(reinterpret_cast<ImTextureID>(view), size);

		ImGui::End();
	}

	// The final image as a picture beside a timeline: the moment a spike happened is easier to read
	// with what was on screen at that moment next to it. The same shared texture as the Preview
	// panel, so it freezes with the timeline and is saved from the same place.
	void Application::DrawFrameImage(float max_width, float max_height)
	{
		if (!IsLive())
		{
			ImGui::TextDisabled(Tr("The final image needs a running game: a capture keeps no pixels."));
			return;
		}
		if (m_preview_resource == 0)
		{
			if (ImGui::Button(TrId("Show the final image")))
				StartPreview(kPreviewFinalImage);
			return;
		}

		if (m_preview_resource == kPreviewFinalImage)
			ImGui::TextUnformatted(Tr("Final image"));
		else
			ImGui::Text(Tr("Resource #%u"), m_preview_resource);
		ImGui::SameLine();
		if (m_preview_capture_hold)
			ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), Tr("(captured frame %llu)"),
				static_cast<unsigned long long>(m_preview_frozen_frame));
		else if (m_preview_frozen_sent)
			ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), Tr("(frozen, frame %llu)"),
				static_cast<unsigned long long>(m_preview_frozen_frame));
		else
			ImGui::TextDisabled(Tr("(live)"));
		if (m_preview_capture_hold)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton(TrId("Back to live")))
				m_preview_capture_hold = false;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Save as PNG")))
			SavePreviewImage();
		if (!m_preview_saved.empty())
			ImGui::TextDisabled("%s", m_preview_saved.c_str());

		if (!m_preview.IsValid() || m_preview_view_this_frame == nullptr)
		{
			ImGui::TextDisabled("%s", m_preview_message.empty() ? Tr("waiting for the game to share the image...")
			                                                   : m_preview_message.c_str());
			return;
		}

		const float texture_width = static_cast<float>(m_preview.Width());
		const float texture_height = static_cast<float>(m_preview.Height());
		const float available_height = std::max(max_height - ImGui::GetCursorPosY(), 40.0f);
		const float scale = std::min(max_width / texture_width, available_height / texture_height);
		ImGui::Image(reinterpret_cast<ImTextureID>(m_preview_view_this_frame),
			ImVec2(texture_width * scale, texture_height * scale));
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Tr("Double click to open it full size in the Preview panel."));
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			ImGui::SetWindowFocus("Preview");
	}

	std::string Application::ProfileOf(uint32_t shader_id) const
	{
		const SessionModel *active = ActiveModel();
		if (active == nullptr)
			return {};

		const SessionModel &model = *active;
		std::lock_guard<std::mutex> lock(model.Mutex());

		const ShaderInfo *shader = model.ShaderById(shader_id);
		if (shader == nullptr)
			return {};

		const char *prefix = "ps";
		switch (shader->stage)
		{
		case ShaderStage::vertex: prefix = "vs"; break;
		case ShaderStage::geometry: prefix = "gs"; break;
		case ShaderStage::hull: prefix = "hs"; break;
		case ShaderStage::domain: prefix = "ds"; break;
		case ShaderStage::compute: prefix = "cs"; break;
		case ShaderStage::mesh: prefix = "ms"; break;
		case ShaderStage::amplification: prefix = "as"; break;
		default: break;
		}

		const uint32_t major = (shader->shader_model >> 4) & 0xF;
		const uint32_t minor = shader->shader_model & 0xF;
		if (major == 0)
			return {};
		return std::string(prefix) + "_" + std::to_string(major) + "_" + std::to_string(minor);
	}

	void Application::InjectHlsl(uint32_t shader_id, const std::string &hlsl, const char *origin,
	                             bool exportable)
	{
		if (!m_client || shader_id == 0 || hlsl.empty())
			return;

		const std::string profile = ProfileOf(shader_id);
		if (profile.empty())
		{
			m_inject_message = "the shader model of this shader is unknown";
			return;
		}

		const CompileResult compiled = CompileHlsl(hlsl, "main", profile, "replacement.hlsl");
		if (!compiled.ok)
		{
			m_inject_message = "compilation failed: " +
				(compiled.messages.empty() ? compiled.error : compiled.messages);
			return;
		}

		std::string message;
		if (m_client->ReplaceShader(shader_id, compiled.byte_code, message))
		{
			m_inject_message = "injected as " + profile + " (" + message + ")";
			// Written down at the moment the user decided, so it can be exported and applied
			// again in a later run, where none of this session's ids mean anything.
			m_mods.RecordReplacement(m_client->Model(), shader_id, compiled.byte_code, hlsl, profile,
				origin, exportable);
		}
		else
		{
			m_inject_message = "the add-on refused the replacement: " + message;
		}
	}

	void Application::HighlightShader(uint32_t shader_id)
	{
		if (!m_client || shader_id == 0)
			return;

		// Highlighting is a replacement like any other: a generated shader that writes magenta to
		// every target the original declares. Building it from the reflected output signature is
		// what keeps it compatible with the pipeline it goes into.
		ShaderAnalysis analysis;
		if (!m_analysis.Result(shader_id, analysis) || !analysis.reflection.ok)
		{
			RequestAnalysis(shader_id);
			m_inject_message = "reflecting the shader first, try again in a moment";
			return;
		}

		if (analysis.reflection.outputs.empty())
		{
			m_inject_message = "this shader writes no render target, so there is nothing to highlight";
			return;
		}

		std::string hlsl = "// Generated by CyGPUInspector to highlight a shader.\n";
		hlsl += "struct Output\n{\n";
		for (const SignatureElement &element : analysis.reflection.outputs)
			hlsl += "    float4 " + element.semantic_name + std::to_string(element.semantic_index) +
				" : " + element.semantic_name + std::to_string(element.semantic_index) + ";\n";
		hlsl += "};\n\nOutput main()\n{\n    Output output;\n";
		for (const SignatureElement &element : analysis.reflection.outputs)
			hlsl += "    output." + element.semantic_name + std::to_string(element.semantic_index) +
				" = float4(1.0f, 0.0f, 1.0f, 1.0f);\n";
		hlsl += "    return output;\n}\n";

		InjectHlsl(shader_id, hlsl);
	}

	SessionModel *Application::ActiveModel()
	{
		if (m_capture_model != nullptr)
			return m_capture_model.get();
		return m_client != nullptr ? &m_client->Model() : nullptr;
	}

	const SessionModel *Application::ActiveModel() const
	{
		return const_cast<Application *>(this)->ActiveModel();
	}

	void Application::RefreshCaptures()
	{
		m_captures = CaptureArchive::List(CaptureArchive::DefaultRoot());
	}

	void Application::SaveCapture()
	{
		if (!m_client)
		{
			m_capture_message = "nothing to capture: no game is connected";
			return;
		}

		// The graph travels with the capture, so an offline capture stays navigable.
		RefreshFrameGraph(true);

		std::string process_name;
		std::filesystem::path directory;
		std::string error;
		bool saved = false;
		{
			SessionModel &model = m_client->Model();
			std::lock_guard<std::mutex> lock(model.Mutex());

			process_name = model.HasSession() ? model.Session().process_name : "Capture";
			directory = CaptureArchive::SuggestDirectory(process_name);
			saved = CaptureArchive::Save(model, m_graph, directory, error);
		}

		if (saved)
		{
			m_capture_message = "saved to " + directory.string();
			RefreshCaptures();
		}
		else
		{
			m_capture_message = "the capture could not be saved: " + error;
		}
	}

	void Application::OpenCapture(const CaptureInfo &info)
	{
		auto model = std::make_unique<SessionModel>();

		CaptureInfo loaded;
		std::string error;
		if (!CaptureArchive::Load(info.path, *model, loaded, error))
		{
			m_capture_message = "could not open the capture: " + error;
			return;
		}

		m_capture_model = std::move(model);
		m_capture_info = loaded;
		m_capture_message = "opened " + loaded.Describe();

		// A capture has its own ids, so nothing selected before it still means anything.
		m_selected_shader = 0;
		m_selected_resource = 0;
		m_selected_event = 0;
		m_selected_pass = 0;
		m_disassembly_shader = 0;
		m_disassembly_lines.clear();
		m_editor_shader = 0;
		m_editor_text.clear();
		m_analysis.Clear();
		m_preview.Close();
		m_preview_resource = 0;

		RefreshFrameGraph(true);
	}

	void Application::CloseCapture()
	{
		m_capture_model.reset();
		m_compare_model.reset();
		m_has_diff = false;
		m_capture_message.clear();
		m_selected_shader = 0;
		m_selected_resource = 0;
		m_selected_event = 0;
		m_selected_pass = 0;
		m_disassembly_shader = 0;
		m_disassembly_lines.clear();
		m_analysis.Clear();
		RefreshFrameGraph(true);
	}

	namespace
	{
		Json Failure(const std::string &message)
		{
			Json response = Json::Object();
			response["ok"] = Json(false);
			response["error"] = Json(message);
			return response;
		}

		Json Success(Json result)
		{
			Json response = Json::Object();
			response["ok"] = Json(true);
			response["result"] = std::move(result);
			return response;
		}

		Json ShaderSummary(const ShaderInfo &shader, const SessionModel &model)
		{
			Json entry = Json::Object();
			entry["id"] = Json(shader.id);
			entry["signature"] = Json(shader.signature.ToHex());
			entry["stage"] = Json(std::string(ShaderStageName(shader.stage)));
			entry["format"] = Json(std::string(ShaderFormatName(shader.format)));
			entry["shader_model"] = Json(std::string(ShaderModelName(shader.shader_model)));
			entry["byte_code_size"] = Json(shader.code_size);
			entry["draws_this_frame"] = Json(shader.draws_this_frame);
			entry["dispatches_this_frame"] = Json(shader.dispatches_this_frame);
			entry["disabled"] = Json(shader.disabled);
			entry["replaced"] = Json(shader.replaced);
			if (shader.gpu_ticks != 0 && model.Timings().IsValid())
				entry["gpu_ms"] = Json(model.Timings().Milliseconds(shader.gpu_ticks));
			return entry;
		}

		Json ResourceSummary(const ResourceInfo &resource)
		{
			Json entry = Json::Object();
			entry["id"] = Json(resource.id);
			if (!resource.name.empty())
				entry["name"] = Json(resource.name);
			entry["kind"] = Json(std::string(ResourceKindName(resource.kind)));
			entry["format"] = Json(std::string(FormatName(resource.format)));
			entry["width"] = Json(resource.width);
			entry["height"] = Json(resource.height);
			entry["mip_levels"] = Json(resource.mip_levels);
			entry["samples"] = Json(resource.samples);
			entry["usage"] = Json(UsageString(resource.usage_flags));
			entry["writes_this_frame"] = Json(resource.writes_this_frame);
			entry["reads_this_frame"] = Json(resource.reads_this_frame);
			entry["alive"] = Json(resource.alive);
			return entry;
		}

		Json EventSummary(const FrameEvent &event, const SessionModel &model)
		{
			Json entry = Json::Object();
			entry["index"] = Json(event.index);
			entry["kind"] = Json(std::string(EventKindName(event.kind)));
			entry["pipeline"] = Json(event.pipeline_id);
			entry["target"] = Json(event.primary_resource);
			entry["depth_target"] = Json(event.secondary_resource);
			entry["a"] = Json(event.a);
			entry["b"] = Json(event.b);
			entry["skipped"] = Json((event.flags & kEventSkipped) != 0);
			entry["replaced"] = Json((event.flags & kEventReplaced) != 0);

			Json shaders = Json::Array();
			for (uint32_t shader_id : model.ShadersOfPipeline(event.pipeline_id))
				shaders.Push(Json(shader_id));
			entry["shaders"] = shaders;

			const auto timing = model.Timings().by_event.find(event.index);
			if (timing != model.Timings().by_event.end() && model.Timings().IsValid())
				entry["gpu_ms"] = Json(model.Timings().Milliseconds(timing->second));
			return entry;
		}
	}

	Json Application::HandleMcpRequest(const Json &request)
	{
		const std::string tool = request["tool"].AsString();
		const Json &arguments = request["arguments"];

		SessionModel *active = ActiveModel();
		if (active == nullptr)
			return Failure("no session is connected and no capture is open");

		SessionModel &model = *active;
		std::lock_guard<std::mutex> lock(model.Mutex());

		const uint32_t shader_id = arguments["shader_id"].AsUInt32();
		const uint32_t resource_id = arguments["resource_id"].AsUInt32();

		// ---- what the frame is ------------------------------------------------------------
		if (tool == "get_session")
		{
			Json result = Json::Object();
			result["live"] = Json(IsLive());
			result["viewing_capture"] = Json(m_capture_model != nullptr);
			if (model.HasSession())
			{
				result["process_name"] = Json(std::string(model.Session().process_name));
				result["api"] = Json(std::string(GraphicsApiName(model.Session().api)));
				result["adapter"] = Json(std::string(model.Session().adapter_name));
			}
			result["shader_count"] = Json(static_cast<uint32_t>(model.Shaders().size()));
			result["resource_count"] = Json(static_cast<uint32_t>(model.Resources().size()));
			return Success(std::move(result));
		}

		if (tool == "get_current_frame")
		{
			const FrameInfo &frame = model.LastFrame();
			Json result = Json::Object();
			result["index"] = Json(frame.index);
			result["draw_count"] = Json(frame.draw_count);
			result["dispatch_count"] = Json(frame.dispatch_count);
			result["event_count"] = Json(static_cast<uint32_t>(frame.events.size()));
			result["dropped_events"] = Json(frame.dropped_events);
			result["cpu_frame_ms"] = Json(static_cast<double>(frame.cpu_frame_ms));
			if (model.Timings().IsValid())
			{
				result["gpu_total_ms"] = Json(model.Timings().Milliseconds(model.Timings().total_ticks));
				result["gpu_frame_index"] = Json(model.Timings().frame_index);
			}
			return Success(std::move(result));
		}

		// ---- shaders ------------------------------------------------------------------------
		if (tool == "list_shaders" || tool == "search_shaders")
		{
			const std::string query = arguments["query"].AsString();
			const bool used_only = arguments["used_this_frame"].AsBool(tool == "search_shaders");
			const uint32_t limit = arguments["limit"].AsUInt32(200);

			Json list = Json::Array();
			for (const ShaderInfo &shader : model.Shaders())
			{
				if (shader.id == 0 || list.Size() >= limit)
					continue;
				if (used_only && shader.draws_this_frame == 0 && shader.dispatches_this_frame == 0)
					continue;

				const std::string haystack = std::string(ShaderStageName(shader.stage)) + " " +
					ShaderFormatName(shader.format) + " " + shader.signature.ToHex();
				if (!query.empty() && !ContainsInsensitive(haystack, query.c_str()))
					continue;

				list.Push(ShaderSummary(shader, model));
			}

			Json result = Json::Object();
			result["shaders"] = list;
			result["returned"] = Json(static_cast<uint32_t>(list.Size()));
			return Success(std::move(result));
		}

		if (tool == "inspect_shader")
		{
			const ShaderInfo *shader = model.ShaderById(shader_id);
			if (shader == nullptr)
				return Failure("unknown shader id");

			Json result = ShaderSummary(*shader, model);
			result["semantic_hash"] = Json(shader->semantic_hash.ToHex());

			ShaderAnalysis analysis;
			if (m_analysis.Result(shader_id, analysis))
			{
				result["disassembled"] = Json(analysis.disassembly.ok);
				result["decompilations"] = Json(static_cast<uint32_t>(analysis.decompilations.size()));
				if (analysis.reflection.ok)
				{
					Json bindings = Json::Array();
					for (const ShaderBinding &binding : analysis.reflection.bindings)
					{
						Json entry = Json::Object();
						entry["kind"] = Json(std::string(BindingKindName(binding.kind)));
						entry["name"] = Json(binding.name);
						entry["slot"] = Json(binding.bind_point);
						bindings.Push(entry);
					}
					result["bindings"] = bindings;
					result["instruction_count"] = Json(analysis.reflection.instruction_count);
				}
			}
			else
			{
				result["disassembled"] = Json(false);
				result["hint"] = Json("call get_shader_disassembly to have it analysed");
			}
			return Success(std::move(result));
		}

		if (tool == "get_shader_disassembly")
		{
			ShaderAnalysis analysis;
			if (!m_analysis.Result(shader_id, analysis))
			{
				RequestAnalysis(shader_id);
				return Failure("the shader is being disassembled, ask again in a moment");
			}
			if (!analysis.disassembly.ok)
				return Failure(analysis.disassembly.error);

			Json result = Json::Object();
			result["shader_id"] = Json(shader_id);
			result["tool"] = Json(analysis.disassembly.tool);
			result["line_count"] = Json(static_cast<uint32_t>(analysis.disassembly.LineCount()));
			result["disassembly"] = Json(analysis.disassembly.text);
			return Success(std::move(result));
		}

		if (tool == "decompile_shader" || tool == "get_shader_decompiled_hlsl")
		{
			ShaderAnalysis analysis;
			if (!m_analysis.Result(shader_id, analysis))
			{
				RequestAnalysis(shader_id);
				return Failure("the shader is being analysed, ask again in a moment");
			}
			if (analysis.decompilations.empty())
			{
				m_analysis.RequestDecompilation(shader_id);
				return Failure("decompilation started, ask again in a moment");
			}

			Json backends = Json::Array();
			for (const DecompilationResult &entry : analysis.decompilations)
			{
				Json item = Json::Object();
				item["backend"] = Json(entry.backend_id);
				item["version"] = Json(entry.backend_version);
				item["verdict"] = Json(std::string(ValidationVerdictName(entry.verdict)));
				item["validation"] = Json(entry.validation_messages);
				item["notes"] = Json(entry.notes);
				item["untranslated_lines"] = Json(entry.untranslated_lines);
				item["hlsl"] = Json(entry.hlsl);
				backends.Push(item);
			}

			Json result = Json::Object();
			result["shader_id"] = Json(shader_id);
			result["reconstructions"] = backends;
			result["warning"] = Json("These are decompiler reconstructions, not the original source. "
			                         "The original source does not exist in a compiled shader.");
			return Success(std::move(result));
		}

		if (tool == "get_shader_draw_calls")
		{
			const std::vector<uint32_t> events = model.EventsUsingShader(shader_id);
			Json list = Json::Array();
			for (uint32_t index : events)
			{
				if (list.Size() >= arguments["limit"].AsUInt32(500))
					break;
				list.Push(Json(index));
			}

			Json result = Json::Object();
			result["shader_id"] = Json(shader_id);
			result["total"] = Json(static_cast<uint32_t>(events.size()));
			result["events"] = list;
			return Success(std::move(result));
		}

		if (tool == "get_shader_timing")
		{
			const ShaderInfo *shader = model.ShaderById(shader_id);
			if (shader == nullptr)
				return Failure("unknown shader id");
			if (!model.Timings().IsValid())
				return Failure("no GPU timings: set the tracking level to Pass Timing or Full Draw Timing");

			const uint32_t calls = shader->draws_this_frame + shader->dispatches_this_frame;
			Json result = Json::Object();
			result["shader_id"] = Json(shader_id);
			result["calls"] = Json(calls);
			result["total_ms"] = Json(model.Timings().Milliseconds(shader->gpu_ticks));
			if (calls != 0)
				result["average_ms"] = Json(model.Timings().Milliseconds(shader->gpu_ticks) / calls);
			result["measured_frame"] = Json(model.Timings().frame_index);
			result["caveat"] = Json("A command's time is the interval until the next recorded command: "
			                        "ReShade calls an add-on before a command, never after.");
			return Success(std::move(result));
		}

		// ---- resources ----------------------------------------------------------------------
		if (tool == "list_resources" || tool == "search_resources")
		{
			const std::string query = arguments["query"].AsString();
			const bool targets_only = arguments["render_targets_only"].AsBool(false);
			const uint32_t limit = arguments["limit"].AsUInt32(200);

			Json list = Json::Array();
			for (const ResourceInfo &resource : model.Resources())
			{
				if (resource.id == 0 || list.Size() >= limit)
					continue;
				if (targets_only && (resource.usage_flags &
				    (kUsageRenderTarget | kUsageDepthStencil | kUsageUnorderedAccess)) == 0)
					continue;

				const std::string haystack = std::string(ResourceKindName(resource.kind)) + " " +
					FormatName(resource.format) + " " + UsageString(resource.usage_flags);
				if (!query.empty() && !ContainsInsensitive(haystack, query.c_str()))
					continue;

				list.Push(ResourceSummary(resource));
			}

			Json result = Json::Object();
			result["resources"] = list;
			result["returned"] = Json(static_cast<uint32_t>(list.Size()));
			return Success(std::move(result));
		}

		if (tool == "inspect_resource")
		{
			const ResourceInfo *resource = model.ResourceById(resource_id);
			if (resource == nullptr)
				return Failure("unknown resource id");

			Json result = ResourceSummary(*resource);
			result["created_frame"] = Json(resource->created_frame);
			result["created_event"] = Json(resource->created_event);
			result["first_write_event"] = Json(resource->first_write_event);
			result["last_write_event"] = Json(resource->last_write_event);
			result["last_read_event"] = Json(resource->last_read_event);
			return Success(std::move(result));
		}

		if (tool == "get_resource_writers" || tool == "get_resource_readers")
		{
			const ResourceFlow *flow = m_graph.FlowOf(resource_id);
			if (flow == nullptr)
				return Failure("this resource was not touched in the analysed frame");

			const std::vector<uint32_t> &passes =
				tool == "get_resource_writers" ? flow->written_by : flow->read_by;

			Json list = Json::Array();
			for (uint32_t pass_id : passes)
			{
				const GraphPass *pass = m_graph.PassById(pass_id);
				Json entry = Json::Object();
				entry["pass_id"] = Json(pass_id);
				entry["pass_name"] = Json(pass != nullptr ? pass->name : std::string("?"));
				if (pass != nullptr)
				{
					entry["first_event"] = Json(pass->first_event);
					entry["last_event"] = Json(pass->last_event);
				}
				list.Push(entry);
			}

			Json result = Json::Object();
			result["resource_id"] = Json(resource_id);
			result["passes"] = list;
			return Success(std::move(result));
		}

		// ---- the frame graph -----------------------------------------------------------------
		if (tool == "get_frame_graph" || tool == "get_passes")
		{
			Json passes = Json::Array();
			for (const GraphPass &pass : m_graph.Passes())
			{
				Json entry = Json::Object();
				entry["id"] = Json(pass.id);
				entry["name"] = Json(pass.name);
				entry["name_origin"] = Json(std::string(PassNameOriginName(pass.origin)));
				entry["confidence"] = Json(static_cast<double>(pass.confidence));
				entry["first_event"] = Json(pass.first_event);
				entry["last_event"] = Json(pass.last_event);
				entry["draw_count"] = Json(pass.draw_count);
				entry["dispatch_count"] = Json(pass.dispatch_count);
				entry["depth_target"] = Json(pass.depth_target);

				Json targets = Json::Array();
				for (uint32_t id : pass.render_targets)
					targets.Push(Json(id));
				entry["render_targets"] = targets;

				Json reads = Json::Array();
				for (uint32_t id : pass.reads)
					reads.Push(Json(id));
				entry["reads"] = reads;

				Json shaders = Json::Array();
				for (uint32_t id : pass.shaders)
					shaders.Push(Json(id));
				entry["shaders"] = shaders;

				passes.Push(entry);
			}

			Json edges = Json::Array();
			if (tool == "get_frame_graph")
			{
				for (const GraphEdge &edge : m_graph.Edges())
				{
					Json entry = Json::Object();
					entry["from_pass"] = Json(edge.from_pass);
					entry["to_pass"] = Json(edge.to_pass);
					entry["resource"] = Json(edge.resource_id);
					edges.Push(entry);
				}
			}

			Json result = Json::Object();
			result["frame"] = Json(m_graph.FrameIndex());
			result["passes"] = passes;
			if (tool == "get_frame_graph")
				result["dependencies"] = edges;
			result["caveat"] = Json("No game names its passes: these names are derived from what each "
			                        "command writes and reads, and each carries its confidence.");
			return Success(std::move(result));
		}

		if (tool == "inspect_draw" || tool == "inspect_dispatch")
		{
			const uint32_t event_index = arguments["event_index"].AsUInt32();
			for (const FrameEvent &event : model.LastFrame().events)
			{
				if (event.index != event_index)
					continue;

				Json result = EventSummary(event, model);
				if (const GraphPass *pass = m_graph.PassOfEvent(event_index))
				{
					result["pass_id"] = Json(pass->id);
					result["pass_name"] = Json(pass->name);
				}
				return Success(std::move(result));
			}
			return Failure("no such event in the analysed frame");
		}

		// The runtime capture, as data: the passes in order with what each cost. Same numbers the
		// Frame timeline panel draws, so an agent and a person see the same frame.
		if (tool == "get_frame_timeline")
		{
			// The same layout the timing view draws, so an agent and a person looking at the
			// same frame are told the same lengths.
			const FrameTimings &timings = model.Timings();
			FrameTimeline timeline;
			timeline.Build(timings, m_graph, model.LastFrame().events);

			Json passes = Json::Array();
			for (size_t i = 0; i < m_graph.Passes().size() && i < timeline.Spans().size(); ++i)
			{
				const GraphPass &pass = m_graph.Passes()[i];
				const FrameTimeline::Span &span = timeline.Spans()[i];
				Json entry = Json::Object();
				entry["pass_id"] = Json(pass.id);
				entry["name"] = Json(pass.name);
				entry["name_origin"] = Json(std::string(PassNameOriginName(pass.origin)));
				entry["name_confidence"] = Json(static_cast<double>(pass.confidence));
				entry["first_event"] = Json(pass.first_event);
				entry["last_event"] = Json(pass.last_event);
				entry["draws"] = Json(pass.draw_count);
				entry["dispatches"] = Json(pass.dispatch_count);
				if (timeline.Timed())
				{
					entry["start_ms"] = Json(span.start);
					entry["gpu_ms"] = Json(span.Length());
					entry["share_of_frame"] = Json(timeline.ShareOf(pass.id));
					// Said rather than implied: a pass with no timestamp of its own was placed
					// between the measurements around it, and its length is an estimate.
					entry["measured"] = Json(span.measured);
				}
				else
				{
					entry["gpu_ms"] = Json();
					entry["measured"] = Json(false);
				}
				passes.Push(entry);
			}

			Json result = Json::Object();
			result["frame_index"] = Json(m_graph.FrameIndex());
			result["timed_frame_index"] = Json(timings.frame_index);
			result["measured"] = Json(timeline.Timed());
			result["measured_positions"] = Json(timeline.MeasuredPositions());
			result["frame_gpu_ms"] = Json(timeline.Timed() ? timeline.Length() : 0.0);
			result["total_gpu_ms"] = Json(timings.Milliseconds(timings.total_ticks));
			result["passes"] = passes;

			// The raw measurements, on request: every timestamp with where it starts and how long
			// it runs until the next one the GPU reached. Large at Full Draw Timing, hence opt in.
			if (arguments["include_measurements"].AsBool())
			{
				Json points = Json::Array();
				for (const FrameTimeline::Point &point : timeline.Points())
				{
					Json entry = Json::Object();
					entry["event"] = Json(point.event);
					entry["kind"] = Json(std::string(EventKindName(point.kind)));
					entry["start_ms"] = Json(point.start);
					entry["gpu_ms"] = Json(point.length);
					points.Push(entry);
				}
				result["measurements"] = points;
			}

			if (!timeline.Timed())
				result["note"] = Json(std::string("no GPU timings: raise the tracking level to "
					"pass_timing or higher"));
			return Success(std::move(result));
		}

		if (tool == "get_capture_state")
		{
			const CaptureStateRecord &capture = model.CaptureState();
			Json result = Json::Object();
			result["stage"] = Json(std::string(CaptureStageName(capture.stage)));
			result["mode"] = Json(std::string(CaptureModeName(capture.mode)));
			result["first_frame"] = Json(capture.first_frame);
			result["frames_requested"] = Json(capture.frames_requested);
			result["frames_done"] = Json(capture.frames_done);
			result["commands_recorded"] = Json(capture.draw_states_recorded);
			result["bindings_recorded"] = Json(capture.bindings_recorded);
			result["barriers_recorded"] = Json(capture.barriers_recorded);
			result["dropped"] = Json(capture.dropped);
			result["running"] = Json(model.DeepCaptureRunning());
			result["frame_available"] = Json(model.HasDeepFrame());
			if (model.CaptureScope().frame_index != 0)
			{
				// Limited to the 3D render of an Unreal viewport by the plugin's markers.
				Json scope = Json::Object();
				scope["frame"] = Json(model.CaptureScope().frame_index);
				scope["first_event"] = Json(model.CaptureScope().first_event);
				scope["last_event"] = Json(model.CaptureScope().last_event);
				result["scope"] = std::move(scope);
			}

			// What the capture left on disk: its folder, and each buffer with the file it went to.
			if (!m_capture_folder.empty() && m_capture_folder_first == capture.first_frame)
				result["folder"] = Json(CaptureFolderText());
			if (m_buffer_writer.Active() && m_buffer_writer.FirstFrame() == capture.first_frame)
			{
				Json buffers = Json::Array();
				for (const CaptureBufferWriter::Item &item : m_buffer_writer.Items())
				{
					Json entry = Json::Object();
					entry["index"] = Json(item.record.index + 1);
					entry["resource_id"] = Json(item.record.resource_id);
					entry["width"] = Json(item.record.width);
					entry["height"] = Json(item.record.height);
					entry["format"] = Json(std::string(FormatName(item.record.source_format)));
					entry["depth"] = Json(item.depth);
					entry["first_event"] = Json(item.record.first_event);
					const bool saved = item.state == CaptureBufferWriter::State::written;
					entry["saved"] = Json(saved);
					if (saved)
					{
						entry["png"] = Json(item.file_stem + ".png");
						entry["dds"] = Json(item.file_stem + ".dds");
					}
					else if (!item.error.empty())
					{
						entry["why"] = Json(item.error);
					}
					buffers.Push(std::move(entry));
				}
				result["buffers"] = std::move(buffers);
				result["buffers_note"] = Json(std::string("each buffer as it was at the end of the captured "
					"frame; the PNG is a view, the DDS holds the data"));
			}
			return Success(std::move(result));
		}

		if (tool == "get_command_state")
		{
			if (!model.HasDeepFrame())
				return Failure("no deep capture has run: call start_deep_capture first");

			const FrameInfo &frame = model.DeepFrame();
			const uint32_t event_index = arguments["event_index"].AsUInt32();
			const CommandState *state = frame.DrawStateOfEvent(event_index);
			if (state == nullptr)
				return Failure("that command has no recorded state; the deep capture records "
				               "draws and dispatches, and only up to its per frame ceiling");

			Json bindings = Json::Array();
			for (const DrawBinding &binding : state->bindings)
			{
				Json entry = Json::Object();
				entry["kind"] = Json(std::string(SlotKindName(static_cast<SlotKind>(binding.kind))));
				entry["slot"] = Json(static_cast<uint32_t>(binding.slot));
				entry["stage"] = Json(std::string(ShaderStageName(static_cast<ShaderStage>(binding.stage))));
				entry["resource_id"] = Json(binding.resource_id);
				if (const ResourceInfo *resource = model.ResourceById(binding.resource_id))
					entry["resource"] = Json(resource->Describe());
				if (binding.size != 0)
					entry["size"] = Json(binding.size);
				if (binding.offset != 0)
					entry["offset"] = Json(binding.offset);
				bindings.Push(entry);
			}

			Json result = Json::Object();
			result["frame_index"] = Json(frame.index);
			result["event_index"] = Json(event_index);
			result["pipeline_id"] = Json(state->record.pipeline_id);
			result["bindings"] = bindings;
			if ((state->record.flags & kDrawStateViewportValid) != 0)
			{
				Json viewport = Json::Object();
				viewport["x"] = Json(static_cast<double>(state->record.viewport[0]));
				viewport["y"] = Json(static_cast<double>(state->record.viewport[1]));
				viewport["width"] = Json(static_cast<double>(state->record.viewport[2]));
				viewport["height"] = Json(static_cast<double>(state->record.viewport[3]));
				result["viewport"] = viewport;
			}
			if (state->TablesUnresolved())
				result["incomplete"] = Json(std::string("this command bound a descriptor table, whose "
					"contents the ReShade add-on API does not expose; the binding list is partial"));

			if (const PipelineStateRecord *pipeline = model.PipelineStateById(state->record.pipeline_id))
			{
				Json fixed = Json::Object();
				fixed["depth_enable"] = Json(pipeline->depth_enable != 0);
				fixed["depth_write"] = Json(pipeline->depth_write != 0);
				fixed["stencil_enable"] = Json(pipeline->stencil_enable != 0);
				fixed["blend_enabled"] = Json(pipeline->blend_enable_mask != 0);
				fixed["cull_mode"] = Json(pipeline->cull_mode);
				fixed["sample_count"] = Json(pipeline->sample_count);
				fixed["reported_fields"] = Json(pipeline->known_fields);
				result["pipeline_state"] = fixed;
			}
			return Success(std::move(result));
		}

		// ---- actions above read only -----------------------------------------------------------
		if (tool == "disable_shader" || tool == "enable_shader" || tool == "highlight_shader" ||
		    tool == "restore_shader")
		{
			if (!IsLive())
				return Failure("a capture is open: there is no game to act on");

			bool ok = false;
			if (tool == "highlight_shader")
			{
				HighlightShader(shader_id);
				ok = m_inject_message.find("injected") != std::string::npos;
			}
			else
			{
				const ShaderCommand command = tool == "disable_shader" ? ShaderCommand::disable
					: tool == "enable_shader" ? ShaderCommand::enable : ShaderCommand::restore;
				ok = m_client->SendShaderCommand(command, shader_id);
			}

			if (!ok)
				return Failure("the add-on refused: " +
					(m_inject_message.empty() ? std::string("unknown shader or no pipeline uses it")
					                          : m_inject_message));

			Json result = Json::Object();
			result["shader_id"] = Json(shader_id);
			result["applied"] = Json(tool);
			return Success(std::move(result));
		}

		if (tool == "start_deep_capture")
		{
			if (!IsLive())
				return Failure("a capture is open: there is no game to arm");

			m_deep_frames = static_cast<int>(arguments["frames"].AsUInt32(1));
			if (m_deep_frames < 1)
				m_deep_frames = 1;
			if (m_deep_frames > 8)
				m_deep_frames = 8;
			m_deep_bindings = arguments["bindings"].AsBool(true);
			m_deep_barriers = arguments["barriers"].AsBool(true);
			m_deep_per_draw_timing = arguments["per_draw_timing"].AsBool(true);
			m_deep_buffers = arguments["buffers"].AsBool(true);

			StartDeepCapture();

			Json result = Json::Object();
			result["frames"] = Json(static_cast<uint32_t>(m_deep_frames));
			result["message"] = Json(m_deep_message);
			// Arming is asynchronous by nature: the frames are recorded over the next presents,
			// so an agent polls get_capture_state rather than assuming it is already done.
			result["next"] = Json(std::string("poll get_capture_state until stage is Finished"));
			return Success(std::move(result));
		}

		if (tool == "capture_frame")
		{
			SaveCapture();
			Json result = Json::Object();
			result["message"] = Json(m_capture_message);
			return m_capture_message.rfind("saved to", 0) == 0 ? Success(std::move(result))
			                                                   : Failure(m_capture_message);
		}

		if (tool == "compile_shader" || tool == "replace_shader")
		{
			const std::string hlsl = arguments["hlsl"].AsString();
			if (hlsl.empty())
				return Failure("no HLSL was given");

			const std::string profile = ProfileOf(shader_id);
			if (profile.empty())
				return Failure("unknown shader id, or its shader model is unknown");

			const CompileResult compiled = CompileHlsl(hlsl, "main", profile, "mcp.hlsl");
			if (!compiled.ok)
				return Failure("compilation failed: " +
					(compiled.messages.empty() ? compiled.error : compiled.messages));

			Json result = Json::Object();
			result["profile"] = Json(profile);
			result["byte_code_size"] = Json(static_cast<uint32_t>(compiled.byte_code.size()));
			result["warnings"] = Json(compiled.messages);

			if (tool == "compile_shader")
				return Success(std::move(result));

			if (!IsLive())
				return Failure("a capture is open: there is no game to inject into");

			std::string message;
			if (!m_client->ReplaceShader(shader_id, compiled.byte_code, message))
				return Failure("the add-on refused the replacement: " + message);

			result["replacement"] = Json(message);
			return Success(std::move(result));
		}

		return Failure("unknown tool \"" + tool + "\"");
	}

	void Application::LoadNotesFor(uint32_t shader_id)
	{
		if (shader_id == m_notes_shader || shader_id == 0)
			return;

		Sha256Digest signature;
		{
			SessionModel *active = ActiveModel();
			if (active == nullptr)
				return;
			std::lock_guard<std::mutex> lock(active->Mutex());
			const ShaderInfo *shader = active->ShaderById(shader_id);
			if (shader == nullptr)
				return;
			signature = shader->signature;
		}

		m_notes = ShaderNotes();
		LoadShaderNotes(m_analysis.Store(), signature, m_notes);   // absent is not an error
		m_notes_shader = shader_id;
		m_notes_signature = signature;
	}

	void Application::SaveNotes()
	{
		if (m_notes_shader == 0 || m_notes_signature.IsZero())
			return;
		SaveShaderNotes(m_analysis.Store(), m_notes_signature, m_notes);
	}

	void Application::DrawNotesEditor(uint32_t shader_id)
	{
		LoadNotesFor(shader_id);
		if (m_notes_shader != shader_id)
			return;

		ImGui::SeparatorText("Tags and notes");

		char name[128] = {};
		std::strncpy(name, m_notes.user_name.c_str(), sizeof(name) - 1);
		ImGui::SetNextItemWidth(240.0f);
		if (ImGui::InputTextWithHint("##name", Tr("give this shader a name"), name, sizeof(name),
		                             ImGuiInputTextFlags_EnterReturnsTrue))
		{
			m_notes.user_name = name;
			SaveNotes();
		}

		if (ImGui::Button(TrId("Tags")))
			ImGui::OpenPopup("tags");
		ImGui::SameLine();
		ImGui::TextUnformatted(m_notes.tags.empty() ? "(none)" : m_notes.TagList().c_str());

		if (ImGui::BeginPopup("tags"))
		{
			bool changed = false;
			for (uint32_t i = 0; i < static_cast<uint32_t>(ShaderTag::count); ++i)
			{
				const ShaderTag tag = static_cast<ShaderTag>(i);
				bool enabled = m_notes.HasTag(tag);
				if (ImGui::Checkbox(ShaderTagName(tag), &enabled))
				{
					m_notes.SetTag(tag, enabled);
					changed = true;
				}
			}
			if (changed)
				SaveNotes();
			ImGui::EndPopup();
		}

		for (const Annotation &annotation : m_notes.annotations)
		{
			// The origin of a sentence is never dropped: a model's guess must not read like a fact.
			const ImVec4 colour = annotation.source == AnnotationSource::ai
				? ImVec4(0.8f, 0.7f, 1.0f, 1.0f)
				: annotation.source == AnnotationSource::heuristic ? ImVec4(0.9f, 0.85f, 0.5f, 1.0f)
				                                                   : ImVec4(0.7f, 0.9f, 0.7f, 1.0f);
			ImGui::TextColored(colour, Tr("[%s]"), annotation.Describe().c_str());
			ImGui::SameLine();
			ImGui::TextWrapped("%s", annotation.text.c_str());
		}

		static const char *const kKinds[] = { "note", "classification", "cleanup", "explanation" };
		ImGui::SetNextItemWidth(140.0f);
		ImGui::Combo("##kind", &m_note_kind, kKinds, IM_ARRAYSIZE(kKinds));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-90.0f);
		const bool submitted = ImGui::InputTextWithHint("##note", Tr("add a note"), m_note_text,
			sizeof(m_note_text), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if ((ImGui::Button(TrId("Add")) || submitted) && m_note_text[0] != '\0')
		{
			Annotation annotation;
			annotation.source = AnnotationSource::user;
			annotation.kind = static_cast<AnnotationKind>(m_note_kind);
			annotation.text = m_note_text;
			m_notes.annotations.push_back(std::move(annotation));
			SaveNotes();
			m_note_text[0] = '\0';
		}
	}

	void Application::CreateAnalysisSnapshot()
	{
		SessionModel *active = ActiveModel();
		if (active == nullptr)
		{
			m_capture_message = "nothing to snapshot";
			return;
		}

		RefreshFrameGraph(true);

		std::filesystem::path directory;
		std::string error;
		bool saved = false;
		std::string summary;
		{
			SessionModel &model = *active;
			std::lock_guard<std::mutex> lock(model.Mutex());

			const std::string process = model.HasSession() ? model.Session().process_name : "Snapshot";
			directory = CaptureArchive::SuggestDirectory(process + "_snapshot");
			saved = CaptureArchive::Save(model, m_graph, directory, error);

			if (saved)
			{
				// A snapshot is a capture plus what an analysis actually needs to read: the derived
				// passes in prose, the heaviest shaders, and the reconstructions already computed.
				const FrameInfo &frame = model.LastFrame();

				summary += "# CyGPUInspector analysis snapshot\n\n";
				summary += "Everything below was extracted by the tool from a real frame. Anything a\n";
				summary += "model adds to it is a reconstruction, not the original source: the original\n";
				summary += "source does not exist in a compiled shader.\n\n";

				if (model.HasSession())
				{
					summary += "- Process: " + std::string(model.Session().process_name) + "\n";
					summary += "- Graphics API: " + std::string(GraphicsApiName(model.Session().api)) + "\n";
					summary += "- Adapter: " + std::string(model.Session().adapter_name) + "\n";
				}
				summary += "- Frame: " + std::to_string(frame.index) + "\n";
				summary += "- Draw calls: " + std::to_string(frame.draw_count) + "\n";
				summary += "- Dispatches: " + std::to_string(frame.dispatch_count) + "\n";
				summary += "- Events: " + std::to_string(frame.events.size()) + "\n\n";

				summary += "## Passes, as derived from resource dependencies\n\n";
				summary += "No game names its passes: these names are derived, and each carries the\n";
				summary += "confidence of the derivation.\n\n";
				for (const GraphPass &pass : m_graph.Passes())
				{
					summary += "- **" + pass.name + "** (" + PassNameOriginName(pass.origin) + ", ";
					summary += std::to_string(static_cast<int>(pass.confidence * 100.0f)) + "%), events " +
						std::to_string(pass.first_event) + "-" + std::to_string(pass.last_event) + ", " +
						std::to_string(pass.draw_count) + " draws, " +
						std::to_string(pass.dispatch_count) + " dispatches\n";

					if (!pass.render_targets.empty())
					{
						summary += "  - writes:";
						for (uint32_t id : pass.render_targets)
						{
							const ResourceInfo *resource = model.ResourceById(id);
							summary += " #" + std::to_string(id);
							if (resource != nullptr)
								summary += " (" + std::to_string(resource->width) + "x" +
									std::to_string(resource->height) + " " +
									FormatName(resource->format) + ")";
						}
						summary += "\n";
					}
					if (!pass.reads.empty())
					{
						summary += "  - reads:";
						for (uint32_t id : pass.reads)
							summary += " #" + std::to_string(id);
						summary += "\n";
					}
					if (!pass.shaders.empty())
					{
						summary += "  - shaders:";
						for (uint32_t id : pass.shaders)
							summary += " " + std::to_string(id);
						summary += "\n";
					}
				}

				summary += "\n## Shaders used in this frame\n\n";
				summary += "| id | stage | model | draws | GPU ms | signature |\n";
				summary += "|---|---|---|---|---|---|\n";
				for (const ShaderInfo &shader : model.Shaders())
				{
					if (shader.id == 0 || (shader.draws_this_frame == 0 && shader.dispatches_this_frame == 0))
						continue;

					char row[256];
					std::snprintf(row, sizeof(row), "| %u | %s | %s | %u | %s | %s |\n", shader.id,
						ShaderStageName(shader.stage), ShaderModelName(shader.shader_model),
						shader.draws_this_frame + shader.dispatches_this_frame,
						(shader.gpu_ticks != 0 && model.Timings().IsValid())
							? std::to_string(model.Timings().Milliseconds(shader.gpu_ticks)).substr(0, 6).c_str()
							: "-",
						shader.ShortSignature().c_str());
					summary += row;
				}
			}
		}

		if (!saved)
		{
			m_capture_message = "the snapshot could not be saved: " + error;
			return;
		}

		// Whatever has already been disassembled or decompiled travels with the snapshot.
		std::error_code code;
		std::filesystem::create_directories(directory / "analysis", code);

		uint32_t exported = 0;
		for (uint32_t shader_id = 1; shader_id < 4096; ++shader_id)
		{
			ShaderAnalysis analysis;
			if (!m_analysis.Result(shader_id, analysis))
				continue;

			const std::string name = analysis.signature.ToShortHex(8);
			if (analysis.disassembly.ok)
			{
				std::ofstream file(directory / "analysis" / (name + ".asm.txt"), std::ios::trunc);
				file << analysis.disassembly.text;
				++exported;
			}
			for (const DecompilationResult &result : analysis.decompilations)
			{
				if (!result.ok)
					continue;
				std::ofstream file(directory / "analysis" /
					(name + "." + result.backend_id + ".hlsl"), std::ios::trunc);
				file << "// " << ValidationVerdictName(result.verdict) << " - "
				     << result.validation_messages << "\n";
				file << result.hlsl;
				++exported;
			}
		}

		summary += "\n## Files\n\n";
		summary += "- `capture.json`, `events.bin`, `shaders/` : the frame itself\n";
		summary += "- `analysis/` : " + std::to_string(exported) +
			" disassembly and reconstruction file(s) already computed\n";

		std::ofstream file(directory / "analysis" / "snapshot.md", std::ios::trunc);
		file << summary;

		m_capture_message = "snapshot written to " + directory.string();
		RefreshCaptures();
	}

	void Application::DrawMcpPanel()
	{
		if (!ImGui::Begin(TrId("MCP")))
		{
			ImGui::End();
			return;
		}

		ImGui::Text(Tr("Server: %s"), m_mcp.IsRunning() ? "listening" : "stopped");
		ImGui::SameLine();
		ImGui::TextDisabled(Tr("(%s)"), kAppPipeName);
		ImGui::Text(Tr("Proxy connected: %s"), m_mcp.IsClientConnected() ? "yes" : "no");
		ImGui::Text(Tr("Tool calls: %llu"), static_cast<unsigned long long>(m_mcp.CallCount()));

		ImGui::SeparatorText("Permission");
		static const char *const kLevels[] = { "Read Only", "Debug Control", "Shader Modification" };
		int level = static_cast<int>(m_mcp.Permission());
		ImGui::SetNextItemWidth(220.0f);
		if (ImGui::Combo("##permission", &level, kLevels, IM_ARRAYSIZE(kLevels)))
			m_mcp.SetPermission(static_cast<McpPermission>(level));
		ImGui::SameLine();
		HelpMarker(Tr("Read Only lets an agent inspect, search, decompile and read timings. "
		              "Debug Control adds disable, enable, highlight and capture. "
		              "Shader Modification adds compile, replace and restore. "
		              "This is decided here, never by the proxy, which runs in whatever process an "
		              "agent launched it from."));

		if (m_mcp.Permission() != McpPermission::read_only)
			ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
				Tr("An agent can change what the game renders at this level."));

		ImGui::SeparatorText("How to connect an agent");
		ImGui::TextWrapped(Tr("Point an MCP client at CyGPUInspectorMCP.exe, which is next to this "
		                      "application. It speaks MCP on stdio and forwards every call here."));

		ImGui::SeparatorText("Activity");
		if (ImGui::SmallButton(TrId("Clear")))
			m_mcp.ClearLog();

		ImGui::BeginChild("mcplog", ImVec2(0.0f, 0.0f));
		for (const std::string &line : m_mcp.Log())
		{
			if (line.find("REFUSED") != std::string::npos)
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "%s", line.c_str());
			else if (line.find("  ! ") != std::string::npos)
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", line.c_str());
			else
				ImGui::TextUnformatted(line.c_str());
		}
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		ImGui::End();
	}

	void Application::DrawCapturesPanel()
	{
		if (!ImGui::Begin(TrId("Captures")))
		{
			ImGui::End();
			return;
		}

		if (ImGui::Button(TrId("Capture frame")))
			SaveCapture();
		ImGui::SameLine();
		HelpMarker(Tr("Freezes the frame currently received, its derived graph and the byte code of "
		              "every shader it used, into a directory you can reopen without the game. The "
		              "runtime actions become unavailable, everything else keeps working."));
		ImGui::SameLine();
		if (ImGui::Button(TrId("AI analysis snapshot")))
			CreateAnalysisSnapshot();
		ImGui::SameLine();
		HelpMarker(Tr("A capture plus everything an analysis needs to read: the derived passes in "
		              "prose with their confidence, the shaders of the frame, and whatever has "
		              "already been disassembled or reconstructed. This is exactly what gets handed "
		              "to a model, so it can be read and corrected first."));
		ImGui::SameLine();
		if (ImGui::Button(TrId("Refresh list")))
			RefreshCaptures();

		if (m_capture_model != nullptr)
		{
			ImGui::Separator();
			ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), Tr("Viewing a capture (read only)"));
			ImGui::TextWrapped("%s", m_capture_info.Describe().c_str());
			if (ImGui::SmallButton(TrId("Close capture")))
			{
				CloseCapture();
				ImGui::End();
				return;
			}
		}

		if (!m_capture_message.empty())
		{
			ImGui::Separator();
			ImGui::TextWrapped("%s", m_capture_message.c_str());
		}

		ImGui::Separator();
		if (m_captures.empty())
			ImGui::TextDisabled(Tr("No capture in %s"), CaptureArchive::DefaultRoot().string().c_str());

		for (const CaptureInfo &info : m_captures)
		{
			ImGui::PushID(info.path.string().c_str());

			ImGui::BulletText("%s", info.Describe().c_str());
			ImGui::Indent();
			ImGui::TextDisabled(Tr("%u events, %u passes, %u resources%s"), info.event_count,
				info.pass_count, info.resource_count,
				info.gpu_milliseconds > 0.0 ? "" : ", no GPU timings");
			if (info.gpu_milliseconds > 0.0)
				ImGui::TextDisabled(Tr("GPU: %.2f ms"), info.gpu_milliseconds);

			if (ImGui::SmallButton(TrId("Open")))
				OpenCapture(info);
			ImGui::SameLine();
			if (ImGui::SmallButton(TrId("Compare with the open one")))
			{
				if (m_capture_model == nullptr)
				{
					m_capture_message = "open a capture first, then compare another one with it";
				}
				else
				{
					auto other = std::make_unique<SessionModel>();
					CaptureInfo loaded;
					std::string error;
					if (CaptureArchive::Load(info.path, *other, loaded, error))
					{
						m_diff = CompareCaptures(*m_capture_model, *other);
						m_compare_model = std::move(other);
						m_compare_info = loaded;
						m_has_diff = true;
					}
					else
					{
						m_capture_message = "could not open the other capture: " + error;
					}
				}
			}
			ImGui::Unindent();
			ImGui::PopID();
		}

		if (m_has_diff)
		{
			ImGui::SeparatorText("Difference");
			ImGui::TextWrapped(Tr("A: %s"), m_capture_info.Describe().c_str());
			ImGui::TextWrapped(Tr("B: %s"), m_compare_info.Describe().c_str());
			ImGui::Spacing();

			auto delta = [](const char *label, int32_t value) {
				const ImVec4 colour = value > 0 ? ImVec4(0.9f, 0.7f, 0.4f, 1.0f)
					: value < 0 ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
				ImGui::TextColored(colour, Tr("%+d %s"), value, label);
			};
			delta("draw calls", m_diff.draw_delta);
			delta("dispatches", m_diff.dispatch_delta);
			delta("events", m_diff.event_delta);
			delta("shaders", m_diff.shader_delta);
			delta("resources", m_diff.resource_delta);

			if (m_diff.gpu_delta_ms != 0.0)
				ImGui::Text(Tr("GPU: %+.2f ms"), m_diff.gpu_delta_ms);

			if (!m_diff.only_in_a.empty())
			{
				ImGui::TextDisabled(Tr("only in A (%zu):"), m_diff.only_in_a.size());
				for (size_t i = 0; i < m_diff.only_in_a.size() && i < 12; ++i)
				{
					ImGui::SameLine();
					ImGui::TextUnformatted(m_diff.only_in_a[i].c_str());
				}
			}
			if (!m_diff.only_in_b.empty())
			{
				ImGui::TextDisabled(Tr("only in B (%zu):"), m_diff.only_in_b.size());
				for (size_t i = 0; i < m_diff.only_in_b.size() && i < 12; ++i)
				{
					ImGui::SameLine();
					ImGui::TextUnformatted(m_diff.only_in_b[i].c_str());
				}
			}
			for (const std::string &note : m_diff.notes)
				ImGui::TextWrapped("%s", note.c_str());
		}

		ImGui::End();
	}

	void Application::RefreshFrameGraph(bool force)
	{
		SessionModel *active = ActiveModel();
		if (active == nullptr)
		{
			m_graph.Clear();
			return;
		}

		SessionModel &model = *active;
		std::lock_guard<std::mutex> lock(model.Mutex());

		const uint64_t frame = model.LastFrame().index;
		if (!force && (!m_graph_auto || frame == m_graph_frame || m_capture_model != nullptr))
			return;

		m_graph.Build(model);
		m_graph_frame = frame;

		if (m_graph.PassById(m_selected_pass) == nullptr && !m_graph.Passes().empty())
		{
			const FrameTimings timings = model.Timings();
			const GraphPass *heaviest = &m_graph.Passes().front();
			uint64_t best = 0;
			for (const GraphPass &pass : m_graph.Passes())
			{
				uint64_t ticks = 0;
				for (const auto &entry : timings.by_event)
					if (entry.first >= pass.first_event && entry.first <= pass.last_event)
						ticks += entry.second;
				if (ticks > best)
				{
					best = ticks;
					heaviest = &pass;
				}
			}
			m_selected_pass = heaviest->id;
		}
	}

	void Application::DrawFrameGraphPanel()
	{
		if (!ImGui::Begin(TrId("Frame graph")))
		{
			ImGui::End();
			return;
		}

		if (ActiveModel() == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		ImGui::Checkbox(TrId("Follow"), &m_graph_auto);
		ImGui::SameLine();
		HelpMarker(Tr("No game names its passes: the ReShade add-on API exposes no debug markers. "
		              "This graph is reconstructed from what each command writes and reads, so every "
		              "name is a derivation, shown with its confidence. Turn Follow off to keep the "
		              "graph of one frame while you inspect it."));
		ImGui::SameLine();
		if (ImGui::SmallButton(TrId("Rebuild")))
			RefreshFrameGraph(true);

		if (!m_graph.IsValid())
		{
			ImGui::TextDisabled(Tr("No frame analysed yet."));
			ImGui::End();
			return;
		}

		ImGui::Text(Tr("Frame %llu: %zu passes, %zu dependencies (%.2f ms)"),
			static_cast<unsigned long long>(m_graph.FrameIndex()), m_graph.Passes().size(),
			m_graph.Edges().size(), m_graph.BuildMilliseconds());
		ImGui::Separator();

		// Per pass GPU time, summed from the per command timings when profiling is on.
		FrameTimings timings;
		if (m_client)
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			timings = m_client->Model().Timings();
		}

		ImGui::BeginChild("passes", ImVec2(0.0f, 0.0f));
		for (const GraphPass &pass : m_graph.Passes())
		{
			ImGui::PushID(static_cast<int>(pass.id));

			// Confidence is shown as colour: a guess must never look like a fact.
			ImVec4 colour(0.65f, 0.65f, 0.65f, 1.0f);
			if (pass.origin == PassNameOrigin::user)
				colour = ImVec4(0.5f, 0.9f, 1.0f, 1.0f);
			else if (pass.confidence >= 0.7f)
				colour = ImVec4(0.55f, 0.9f, 0.55f, 1.0f);
			else if (pass.confidence >= 0.4f)
				colour = ImVec4(0.9f, 0.85f, 0.5f, 1.0f);

			char label[160];
			std::snprintf(label, sizeof(label), "%s##pass%u", pass.name.c_str(), pass.id);

			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			const bool selected = ImGui::Selectable(label, m_selected_pass == pass.id);
			ImGui::PopStyleColor();

			if (selected)
			{
				m_selected_pass = pass.id;
				m_selected_event = pass.first_event;
				m_pass_rename[0] = '\0';
			}

			ImGui::Indent();

			if (timings.IsValid())
			{
				uint64_t ticks = 0;
				for (const auto &entry : timings.by_event)
					if (entry.first >= pass.first_event && entry.first <= pass.last_event)
						ticks += entry.second;
				if (ticks != 0)
					ImGui::TextDisabled(Tr("%.3f ms"), timings.Milliseconds(ticks));
			}

			if (pass.draw_count != 0)
				ImGui::TextDisabled(Tr("%u draws, events %u-%u"), pass.draw_count, pass.first_event, pass.last_event);
			else if (pass.dispatch_count != 0)
				ImGui::TextDisabled(Tr("%u dispatches, events %u-%u"), pass.dispatch_count, pass.first_event,
					pass.last_event);
			else
				ImGui::TextDisabled(Tr("%u commands, events %u-%u"), pass.event_count, pass.first_event,
					pass.last_event);

			if (pass.origin != PassNameOrigin::user && pass.confidence > 0.0f)
				ImGui::TextDisabled(Tr("name: %s, confidence %.0f%%"), PassNameOriginName(pass.origin),
					pass.confidence * 100.0f);

			if (m_selected_pass == pass.id)
			{
				if (!pass.render_targets.empty())
				{
					ImGui::TextUnformatted(Tr("writes:"));
					for (uint32_t resource_id : pass.render_targets)
					{
						ImGui::SameLine();
						char button[32];
						std::snprintf(button, sizeof(button), "#%u##w%u", resource_id, resource_id);
						if (ImGui::SmallButton(button))
							m_selected_resource = resource_id;
					}
				}
				if (pass.depth_target != 0)
				{
					ImGui::Text(Tr("depth:  #%u"), pass.depth_target);
				}
				if (!pass.reads.empty())
				{
					ImGui::TextUnformatted(Tr("reads: "));
					for (uint32_t resource_id : pass.reads)
					{
						ImGui::SameLine();
						char button[32];
						std::snprintf(button, sizeof(button), "#%u##r%u", resource_id, resource_id);
						if (ImGui::SmallButton(button))
							m_selected_resource = resource_id;
					}
				}
				if (!pass.shaders.empty())
				{
					ImGui::TextUnformatted(Tr("shaders:"));
					for (uint32_t shader_id : pass.shaders)
					{
						ImGui::SameLine();
						char button[32];
						std::snprintf(button, sizeof(button), "%u##s%u", shader_id, shader_id);
						if (ImGui::SmallButton(button))
							m_selected_shader = shader_id;
					}
				}

				ImGui::SetNextItemWidth(180.0f);
				if (ImGui::InputTextWithHint("##rename", Tr("rename this pass"), m_pass_rename,
				                             sizeof(m_pass_rename), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					m_graph.RenamePass(pass.id, m_pass_rename);
					m_pass_rename[0] = '\0';
				}
			}
			ImGui::Unindent();
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::End();
	}

	void Application::DrawShaderCodePanel()
	{
		if (!ImGui::Begin(TrId("Shader code")))
		{
			ImGui::End();
			return;
		}

		if (ActiveModel() == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		if (m_selected_shader == 0)
		{
			ImGui::TextDisabled(Tr("Select a shader."));
			ImGui::End();
			return;
		}

		// Asking again is free: the service ignores work already done or queued.
		RequestAnalysis(m_selected_shader);
		RefreshDisassemblyLines();

		ShaderAnalysis analysis;
		const bool has_analysis = m_analysis.Result(m_selected_shader, analysis);

		if (ImGui::BeginTabBar("shadercode"))
		{
			if (ImGui::BeginTabItem(TrId("Disassembly")))
			{
				if (!has_analysis)
				{
					ImGui::TextDisabled(Tr("Disassembling shader #%u..."), m_selected_shader);
				}
				else if (!analysis.disassembly.ok)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
						analysis.disassembly.error.c_str());

					// A missing DXIL backend is the common case: say exactly what to install.
					const ToolInfo &dxil = DxilToolInfo();
					if (!dxil.available)
					{
						ImGui::Separator();
						ImGui::TextWrapped("%s", dxil.error.c_str());
						ImGui::TextWrapped(Tr("Copy dxcompiler.dll (and dxil.dll) next to "
						                      "CyGPUInspectorApp.exe to enable Shader Model 6 support."));
					}
				}
				else
				{
					ImGui::SetNextItemWidth(220.0f);
					ImGui::InputTextWithHint("##disasmfilter", Tr("filter lines"), m_disassembly_filter,
						sizeof(m_disassembly_filter));
					ImGui::SameLine();
					ImGui::Checkbox(TrId("Wrap"), &m_disassembly_wrap);
					ImGui::SameLine();
					if (ImGui::SmallButton(TrId("Copy all")))
						ImGui::SetClipboardText(analysis.disassembly.text.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled(Tr("%zu lines, %s%s"), m_disassembly_lines.size(),
						analysis.from_cache ? "from cache" : analysis.disassembly.tool.c_str(),
						analysis.from_cache ? "" : "");

					ImGui::Separator();

					const bool filtering = m_disassembly_filter[0] != '\0';
					ImGui::BeginChild("disasmtext", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
						m_disassembly_wrap ? 0 : ImGuiWindowFlags_HorizontalScrollbar);

					if (filtering)
					{
						// A filtered view is short by construction: no clipper needed.
						for (size_t i = 0; i < m_disassembly_lines.size(); ++i)
						{
							if (!ContainsInsensitive(m_disassembly_lines[i], m_disassembly_filter))
								continue;
							ImGui::TextDisabled("%5zu", i);
							ImGui::SameLine();
							if (m_disassembly_wrap)
								ImGui::TextWrapped("%s", m_disassembly_lines[i].c_str());
							else
								ImGui::TextUnformatted(m_disassembly_lines[i].c_str());
						}
					}
					else
					{
						ImGuiListClipper clipper;
						clipper.Begin(static_cast<int>(m_disassembly_lines.size()));
						while (clipper.Step())
						{
							for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
							{
								ImGui::TextDisabled("%5d", row);
								ImGui::SameLine();
								if (m_disassembly_wrap)
									ImGui::TextWrapped("%s", m_disassembly_lines[static_cast<size_t>(row)].c_str());
								else
									ImGui::TextUnformatted(m_disassembly_lines[static_cast<size_t>(row)].c_str());
							}
						}
					}
					ImGui::EndChild();
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(TrId("Bindings")))
			{
				if (!has_analysis)
				{
					ImGui::TextDisabled(Tr("Reflecting shader #%u..."), m_selected_shader);
				}
				else if (!analysis.reflection.ok)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s",
						analysis.reflection.error.c_str());
				}
				else
				{
					const ReflectionResult &reflection = analysis.reflection;
					ImGui::Text(Tr("%u instructions, %u constant buffers (%s)"), reflection.instruction_count,
						reflection.constant_buffer_count, reflection.tool.c_str());
					if (reflection.thread_group[0] != 0)
						ImGui::Text(Tr("Thread group: %u x %u x %u"), reflection.thread_group[0],
							reflection.thread_group[1], reflection.thread_group[2]);

					ImGui::SeparatorText("Bound resources");
					constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
						ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

					if (reflection.bindings.empty())
					{
						ImGui::TextDisabled(Tr("This shader binds nothing."));
					}
					else if (ImGui::BeginTable("bindings", 5, kFlags))
					{
						ImGui::TableSetupColumn(TrId("Kind"));
						ImGui::TableSetupColumn(TrId("Slot"));
						ImGui::TableSetupColumn(TrId("Count"));
						ImGui::TableSetupColumn(TrId("Size"));
						ImGui::TableSetupColumn(TrId("Name"), ImGuiTableColumnFlags_WidthStretch);
						ImGui::TableHeadersRow();

						for (const ShaderBinding &binding : reflection.bindings)
						{
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(BindingKindName(binding.kind));
							ImGui::TableNextColumn();
							if (binding.space != 0)
								ImGui::Text(Tr("%u (space %u)"), binding.bind_point, binding.space);
							else
								ImGui::Text("%u", binding.bind_point);
							ImGui::TableNextColumn();
							ImGui::Text("%u", binding.bind_count);
							ImGui::TableNextColumn();
							if (binding.size != 0)
								ImGui::Text(Tr("%u B"), binding.size);
							else
								ImGui::TextDisabled("-");
							ImGui::TableNextColumn();
							ImGui::TextUnformatted(binding.name.c_str());
						}
						ImGui::EndTable();
					}

					ImGui::SeparatorText("Signatures");
					ImGui::Columns(2, "signatures", false);
					ImGui::TextDisabled(Tr("Inputs"));
					for (const SignatureElement &element : reflection.inputs)
						ImGui::BulletText(Tr("%s  (%s, v%u)"), element.Describe().c_str(),
							element.component_type.c_str(), element.register_index);
					ImGui::NextColumn();
					ImGui::TextDisabled(Tr("Outputs"));
					for (const SignatureElement &element : reflection.outputs)
						ImGui::BulletText(Tr("%s  (%s, o%u)"), element.Describe().c_str(),
							element.component_type.c_str(), element.register_index);
					ImGui::Columns(1);
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(TrId("HLSL")))
			{
				if (!has_analysis)
				{
					ImGui::TextDisabled(Tr("Waiting for the disassembly of shader #%u..."), m_selected_shader);
				}
				else if (analysis.decompilations.empty())
				{
					ImGui::TextWrapped(Tr("Nothing shown here will ever be the original source: that does "
					                      "not exist in the compiled shader. Every backend that can handle "
					                      "this shader produces its own reconstruction, and they are kept "
					                      "side by side so they can be compared."));
					ImGui::Spacing();
					if (m_analysis.IsPending(m_selected_shader))
						ImGui::TextDisabled(Tr("Decompiling..."));
					else if (ImGui::Button(TrId("Decompile")))
						m_analysis.RequestDecompilation(m_selected_shader);

					ImGui::Spacing();
					ImGui::TextDisabled(Tr("Available backends:"));
					for (const DecompilerBackend *backend : DecompilerRegistry::Instance().All())
						ImGui::BulletText(Tr("%s %s (%s)"), backend->Info().name.c_str(),
							backend->Info().version.c_str(), backend->Info().license.c_str());
				}
				else
				{
					if (m_selected_backend >= static_cast<int>(analysis.decompilations.size()))
						m_selected_backend = 0;

					// One tab per backend: no result is ever merged into or hidden by another.
					if (ImGui::BeginTabBar("backends"))
					{
						for (size_t i = 0; i < analysis.decompilations.size(); ++i)
						{
							const DecompilationResult &result = analysis.decompilations[i];
							if (!ImGui::BeginTabItem(result.backend_id.c_str()))
								continue;

							m_selected_backend = static_cast<int>(i);

							ImVec4 colour(0.9f, 0.6f, 0.3f, 1.0f);
							if (result.verdict == ValidationVerdict::equivalent_disassembly)
								colour = ImVec4(0.55f, 0.9f, 0.55f, 1.0f);
							else if (result.verdict == ValidationVerdict::compiles)
								colour = ImVec4(0.9f, 0.85f, 0.5f, 1.0f);

							ImGui::TextColored(colour, "%s", ValidationVerdictName(result.verdict));
							ImGui::SameLine();
							ImGui::TextDisabled(Tr("(%s)"), result.validation_messages.c_str());
							ImGui::SameLine();
							if (ImGui::SmallButton(TrId("Copy")))
								ImGui::SetClipboardText(result.hlsl.c_str());

							ImGui::TextDisabled(Tr("AI reconstruction: none. This is a decompiler output, "
							                       "version %s, in %.1f ms."), result.backend_version.c_str(),
								result.milliseconds);
							if (!result.notes.empty())
								ImGui::TextWrapped("%s", result.notes.c_str());

							// Edit, compile and inject: the point of a reconstruction is to change it.
							if (m_editor_shader != m_selected_shader || m_editor_text.empty())
							{
								m_editor_text = result.hlsl;
								m_editor_shader = m_selected_shader;
							}

							if (ImGui::SmallButton(TrId("Reset to backend output")))
								m_editor_text = result.hlsl;
							ImGui::SameLine();
							if (ImGui::SmallButton(TrId("Compile")))
							{
								const std::string profile = ProfileOf(m_selected_shader);
								const CompileResult check = CompileHlsl(m_editor_text, "main", profile,
									"edited.hlsl");
								m_inject_message = check.ok
									? "compiles as " + profile
									: "compilation failed: " + (check.messages.empty() ? check.error
										: check.messages);
							}
							ImGui::SameLine();
							ImGui::BeginDisabled(!IsLive());
							if (ImGui::SmallButton(TrId("Compile & Inject")))
								InjectHlsl(m_selected_shader, m_editor_text);
							ImGui::SameLine();
							if (ImGui::SmallButton(TrId("Restore original")))
							{
								m_client->SendShaderCommand(ShaderCommand::restore, m_selected_shader);
								m_inject_message = "the original shader is back";
							}
							ImGui::EndDisabled();

							if (!m_inject_message.empty())
								ImGui::TextWrapped("%s", m_inject_message.c_str());

							ImGui::Separator();
							ImGui::InputTextMultiline("##hlsl", &m_editor_text[0], m_editor_text.size() + 1,
								ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_AllowTabInput |
								ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData *data) {
									if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
									{
										std::string *text = static_cast<std::string *>(data->UserData);
										text->resize(static_cast<size_t>(data->BufTextLen));
										data->Buf = &(*text)[0];
									}
									return 0;
								}, &m_editor_text);

							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(TrId("AI Analysis")))
			{
				ImGui::TextDisabled(Tr("AI assistance arrives with milestone 9."));
				ImGui::Spacing();
				ImGui::TextWrapped(Tr("It will work on the disassembly and the reconstructed HLSL, never "
				                      "instead of them, and every result will be labelled as an AI "
				                      "reconstruction with its confidence."));
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(TrId("Tools")))
			{
				const ToolInfo &dxbc = DxbcToolInfo();
				const ToolInfo &dxil = DxilToolInfo();

				ImGui::SeparatorText("DXBC (Shader Model 4 and 5)");
				if (dxbc.available)
					ImGui::TextWrapped(Tr("%s\n%s"), dxbc.name.c_str(), dxbc.path.c_str());
				else
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", dxbc.error.c_str());

				ImGui::SeparatorText("DXIL (Shader Model 6)");
				if (dxil.available)
					ImGui::TextWrapped(Tr("%s\n%s"), dxil.name.c_str(), dxil.path.c_str());
				else
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", dxil.error.c_str());

				ImGui::SeparatorText("Shader store");
				ImGui::TextWrapped("%s", m_analysis.StorePath().c_str());
				ImGui::TextDisabled(Tr("%zu shader(s) queued for analysis"), m_analysis.PendingCount());
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void Application::DrawDetailsPanel()
	{
		if (!ImGui::Begin(TrId("Details")))
		{
			ImGui::End();
			return;
		}

		if (ActiveModel() == nullptr)
		{
			ImGui::TextDisabled(Tr("Not connected, and no capture open."));
			ImGui::End();
			return;
		}

		SessionModel &model = *ActiveModel();
		std::unique_lock<std::mutex> lock(model.Mutex());

		if (ImGui::BeginTabBar("details"))
		{
			if (ImGui::BeginTabItem(TrId("Shader")))
			{
				const ShaderInfo *shader = model.ShaderById(m_selected_shader);
				if (shader == nullptr)
				{
					ImGui::TextDisabled(Tr("Select a shader."));
				}
				else
				{
					ImGui::Text(Tr("Shader #%u"), shader->id);
					ImGui::Text(Tr("Stage:         %s"), ShaderStageName(shader->stage));
					ImGui::Text(Tr("Format:        %s"), ShaderFormatName(shader->format));
					ImGui::Text(Tr("Shader model:  %s"), ShaderModelName(shader->shader_model));
					ImGui::Text(Tr("Byte code:     %u bytes (%zu received)"), shader->code_size, shader->code.size());
					ImGui::Text(Tr("Draws (frame): %u"), shader->draws_this_frame);
					ImGui::Text(Tr("Dispatches:    %u"), shader->dispatches_this_frame);
					ImGui::Separator();
					ImGui::TextUnformatted(Tr("SHA-256"));
					ImGui::TextWrapped("%s", shader->signature.ToHex().c_str());
					ImGui::TextUnformatted(Tr("Semantic hash"));
					ImGui::TextWrapped("%s", shader->semantic_hash.ToHex().c_str());

					ImGui::Separator();
					const uint32_t shader_id = shader->id;
					const bool disabled = shader->disabled;
					lock.unlock();

					// A capture is read only by construction: there is no game to talk to.
					ImGui::BeginDisabled(!IsLive());
					if (ImGui::Button(disabled ? "Enable" : "Disable"))
					{
						m_client->SendShaderCommand(disabled ? ShaderCommand::enable : ShaderCommand::disable,
							shader_id);
						if (disabled)
							m_mods.Forget(m_client->Model(), shader_id);
						else
							m_mods.RecordDisable(m_client->Model(), shader_id);
					}
					ImGui::SameLine();
					if (ImGui::Button(TrId("Highlight")))
						HighlightShader(shader_id);
					ImGui::SameLine();
					if (ImGui::Button(TrId("Restore")))
					{
						m_client->SendShaderCommand(ShaderCommand::restore, shader_id);
						// Undone in the game, so it is dropped from the export too: exporting a
						// change the user took back would be exporting a mistake.
						m_mods.Forget(m_client->Model(), shader_id);
						m_inject_message.clear();
					}
					ImGui::SameLine();
					HelpMarker(Tr("Disable makes the add-on skip every draw whose pipeline uses this "
					              "shader. Highlight replaces it with a generated shader that writes "
					              "magenta to the same targets. Both are the fastest way to confirm what "
					              "a shader contributes to the image."));
					ImGui::EndDisabled();
					if (!IsLive())
						ImGui::TextDisabled(Tr("Runtime actions need a connected game."));
					if (!m_inject_message.empty())
						ImGui::TextWrapped("%s", m_inject_message.c_str());

					DrawNotesEditor(shader_id);
					lock.lock();
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(TrId("Resource")))
			{
				const ResourceInfo *resource = model.ResourceById(m_selected_resource);
				if (resource == nullptr)
				{
					ImGui::TextDisabled(Tr("Select a resource."));
				}
				else
				{
					ImGui::Text(Tr("Resource #%u"), resource->id);
					ImGui::Text(Tr("Kind:          %s"), ResourceKindName(resource->kind));
					if (resource->kind == ResourceKind::buffer)
					{
						ImGui::Text(Tr("Size:          %llu bytes"),
							static_cast<unsigned long long>(resource->buffer_size));
					}
					else
					{
						ImGui::Text(Tr("Size:          %u x %u x %u"), resource->width, resource->height,
							resource->depth_or_layers);
						ImGui::Text(Tr("Format:        %s (%s%s)"), FormatName(resource->format),
							FormatIsDepth(resource->format) ? "depth" : "color",
							FormatIsSrgb(resource->format) ? ", sRGB" : "");
						ImGui::Text(Tr("Mips:          %u"), resource->mip_levels);
						ImGui::Text(Tr("Samples:       %u"), resource->samples);
					}
					ImGui::Text(Tr("Usage:         %s"), UsageString(resource->usage_flags).c_str());
					ImGui::Text(Tr("Native handle: 0x%016llX"),
						static_cast<unsigned long long>(resource->native_handle));
					ImGui::Text(Tr("Created:       frame %llu, event %u"),
						static_cast<unsigned long long>(resource->created_frame), resource->created_event);
					if (resource->alive)
						ImGui::TextUnformatted(Tr("Destroyed:     still alive"));
					else
						ImGui::Text(Tr("Destroyed:     frame %llu, event %u"),
							static_cast<unsigned long long>(resource->destroyed_frame),
							resource->destroyed_event);

					ImGui::Separator();
					ImGui::Text(Tr("Writes (frame): %u   (%llu total)"), resource->writes_this_frame,
						static_cast<unsigned long long>(resource->total_writes));
					ImGui::Text(Tr("Reads (frame):  %u"), resource->reads_this_frame);
					ImGui::Text(Tr("First write:    event %u"), resource->first_write_event);
					ImGui::Text(Tr("Last write:     event %u"), resource->last_write_event);
					ImGui::Text(Tr("Last read:      event %u"), resource->last_read_event);

					// Section 7 of the brief: who produced this resource, and who consumed it.
					if (const ResourceFlow *flow = m_graph.FlowOf(resource->id))
					{
						ImGui::SeparatorText("Dependencies (this frame)");

						auto pass_list = [&](const char *label, const std::vector<uint32_t> &passes) {
							if (passes.empty())
								return;
							ImGui::TextUnformatted(label);
							for (uint32_t pass_id : passes)
							{
								ImGui::SameLine();
								const GraphPass *pass = m_graph.PassById(pass_id);
								char button[96];
								std::snprintf(button, sizeof(button), "%s##%s%u",
									pass != nullptr ? pass->name.c_str() : "?", label, pass_id);
								if (ImGui::SmallButton(button))
									m_selected_pass = pass_id;
							}
						};

						auto resource_list = [&](const char *label, const std::vector<uint32_t> &ids) {
							if (ids.empty())
								return;
							ImGui::TextUnformatted(label);
							for (uint32_t id : ids)
							{
								ImGui::SameLine();
								char button[32];
								std::snprintf(button, sizeof(button), "#%u##%s%u", id, label, id);
								if (ImGui::SmallButton(button))
									m_selected_resource = id;
							}
						};

						if (flow->cleared)
							ImGui::TextDisabled(Tr("cleared this frame"));
						pass_list("Written by:", flow->written_by);
						pass_list("Read by:   ", flow->read_by);
						resource_list("Copied from:", flow->copied_from);
						resource_list("Copied to:  ", flow->copied_to);
						resource_list("Resolved from:", flow->resolved_from);
						resource_list("Resolved to:  ", flow->resolved_to);
					}

					ImGui::Separator();

					const uint32_t resource_id = resource->id;
					const bool previewing = m_preview_resource == resource_id;
					lock.unlock();
					if (ImGui::Button(previewing ? "Stop preview" : "Preview"))
					{
						if (previewing)
							StopPreview();
						else
							StartPreview(resource_id);
					}
					ImGui::SameLine();
					HelpMarker(Tr("The add-on copies this resource into a texture shared with this "
					              "process, on the game's own queue. The pixels never go through "
					              "system memory."));
					lock.lock();
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(TrId("Event")))
			{
				const FrameInfo &frame = model.LastFrame();
				const FrameEvent *event = nullptr;
				for (const FrameEvent &candidate : frame.events)
				{
					if (candidate.index == m_selected_event)
					{
						event = &candidate;
						break;
					}
				}

				if (event == nullptr)
				{
					ImGui::TextDisabled(Tr("Select an event."));
				}
				else
				{
					ImGui::Text(Tr("Event %u — %s"), event->index, EventKindName(event->kind));
					ImGui::Text(Tr("Command list:   %u"), event->queue_index);
					ImGui::Text(Tr("Pipeline:       %u"), event->pipeline_id);
					ImGui::Text(Tr("Render target:  #%u"), event->primary_resource);
					ImGui::Text(Tr("Depth target:   #%u"), event->secondary_resource);
					ImGui::Text(Tr("a/b/c/d:        %u / %u / %u / %u"), event->a, event->b, event->c, event->d);
					ImGui::Separator();
					for (uint32_t shader_id : model.ShadersOfPipeline(event->pipeline_id))
					{
						if (const ShaderInfo *shader = model.ShaderById(shader_id))
							ImGui::BulletText(Tr("%s shader #%u (%s)"), ShaderStageName(shader->stage), shader->id,
								shader->ShortSignature().c_str());
					}
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(TrId("Log")))
			{
				for (const std::string &line : model.Log())
					ImGui::TextUnformatted(line.c_str());
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void Application::DrawStatusBar()
	{
		if (!ImGui::Begin(TrId("Status")))
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted(m_status.empty() ? "Ready." : m_status.c_str());

		if (m_client)
		{
			SessionModel &model = *ActiveModel();
			std::lock_guard<std::mutex> lock(model.Mutex());
			const StatsRecord &stats = model.Stats();
			const FrameInfo &frame = model.LastFrame();

			ImGui::Separator();
			ImGui::Text(Tr("IPC: %.2f MiB/s, %zu bytes pending, %llu dropped"), m_client->MegabytesPerSecond(),
				m_client->PendingBytes(), static_cast<unsigned long long>(m_client->DroppedBytes()));
			ImGui::Text(Tr("Game frame: %.2f ms, add-on cost: %.3f ms"), frame.cpu_frame_ms, frame.addon_cpu_ms);
			ImGui::Text(Tr("Tracked in game: %u shaders, %u pipelines, %u resources"), stats.tracked_shaders,
				stats.tracked_pipelines, stats.tracked_resources);
			if (!m_client->WriterAlive())
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), Tr("The game process stopped writing."));
		}

		ImGui::End();
	}
}
