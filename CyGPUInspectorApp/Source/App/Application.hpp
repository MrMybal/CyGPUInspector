// CyGPUInspectorApp — the analysis application itself.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Analysis/FrameGraph.hpp"
#include "Assistant/Assistant.hpp"
#include "Editor/HlslEditor.hpp"
#include "Analysis/FrameTimeline.hpp"
#include "Analysis/FrameTrack.hpp"
#include "Mcp/McpBridge.hpp"
#include "Analysis/ShaderAnalysisService.hpp"
#include "Analysis/ShaderQuery.hpp"
#include "Render/CaptureBufferWriter.hpp"
#include "Render/PreviewRenderer.hpp"
#include "Render/SharedTexture.hpp"
#include "Mod/ModRecorder.hpp"
#include "Session/CaptureArchive.hpp"

#include <CyGPUInspectorDatabase/GameProfile.hpp>
#include <CyGPUInspectorDatabase/ShaderNotes.hpp>
#include "Session/SessionClient.hpp"

#include <CyGPUInspectorCore/SessionDirectory.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cygi
{
	class Application
	{
	public:
		// The D3D11 device opens the textures the game shares with us; the context draws them.
		Application(ID3D11Device *device, ID3D11DeviceContext *context);
		~Application();

		// One UI frame.
		void Draw(double delta_seconds);

		bool WantsExit() const { return m_wants_exit; }

		// Attaches to a running session without waiting for a click. `process_id` of 0 means "the
		// only one there is", and it refuses rather than guessing when several are running. It
		// exists for scripted runs and for the demo launcher; the interface is otherwise the same.
		bool ConnectToFirstSession(uint32_t process_id, std::string &message);

		// The adapter the interface was created on. A shared texture can only be opened on the
		// adapter that created it, so the preview compares this with the game's.
		void SetAdapterName(const std::string &name) { m_adapter_name = name; }

		// The logo drawn in the menu bar and the About window. Owned by the caller, which releases it
		// after the application is gone: this file stays free of <d3d11.h> and the Windows macros.
		void SetLogo(ID3D11ShaderResourceView *logo) { m_logo = logo; }

		// Sets the tracking level on the session this application is connected to. Same thing the
		// Connections panel does, reachable from a command line so a scripted run does not depend
		// on someone clicking through a combo box.
		bool SetTrackingLevel(TrackingLevel level, std::string &message);

		// Also for scripted runs, and for the screenshots of the documentation: select a shader as
		// soon as the session has sent it, decompile it if asked, and bring a view forward —
		// "hlsl", "ai-analysis", "hlsl-editor" or "assistant". No click, no cursor moved.
		void RequestStartupShader(uint32_t shader_id, bool decompile);
		void RequestStartupView(const std::string &view);
		// Fills the search of the Shaders panel, in the query language of find_shaders.
		void SetShaderSearch(const std::string &query);

	private:
		void BuildDefaultLayout(unsigned int dockspace_id);
		void ApplyStartupRequests();
		void DrawMenuBar();
		void DrawAboutWindow();
		void DrawConnectionsPanel();
		void DrawShadersPanel();
		void DrawResourcesPanel();
		void DrawEventsPanel();
		void DrawPreviewPanel();
		// The final image beside a timeline, and the controls shared with the Preview panel.
		void DrawFrameImage(float max_width, float max_height);
		void SavePreviewImage();
		void ApplyPreviewUpdate(const PreviewReadyRecord &record);
		// What a capture leaves on disk. Defined in CaptureFiles.cpp.
		std::string GameName();
		std::filesystem::path CaptureFolder(uint64_t first_frame, uint64_t last_frame);
		// The folder of the last capture in UTF-8, for the interface and for MCP; empty if none.
		std::string CaptureFolderText() const;
		void UpdateCaptureBuffers();
		void DrawCaptureFiles(float width, float height);
		void DrawBufferList(const std::vector<CaptureBufferWriter::Item> &items, float width, float height);
		void ShowBufferFile(int index, const std::filesystem::path &png);
		void ReleaseBufferView();
		void OpenCaptureFolder();
		void DrawShaderCodePanel();
		void DrawFrameGraphPanel();
		// The two captures, each with its own panel. Defined in CapturePanels.cpp.
		void DrawTimelinePanel();
		void DrawDeepCapturePanel();
		void DrawTimingView();
		void DrawPassList(const FrameTimings &timings);
		// Selects a command and asks the Frame events list to bring it into view. Selecting a pass
		// somewhere else is worth nothing if the list of its commands stays where it was.
		void SelectEvent(uint32_t index);
		void DrawSelectedPass(const FrameTimings &timings);
		void DrawFrameStrip(uint64_t measured_frame, uint64_t visible_first, uint64_t visible_last, bool pick);
		// The continuous view and the frame picked in it. Defined in ContinuousTimeline.cpp.
		void UpdateFrameTrack();
		void DrawContinuousView();
		void DrawTrackFrameDetail();
		void DrawBindingTable(const CommandState &state);
		void DrawPipelineStateTable(uint32_t pipeline_id);
		void StartDeepCapture();
		void DrawCapturesPanel();
		// Exporting what you changed, so CyGPUInjector can apply it later. In ModExportPanel.cpp.
		void DrawModExportPanel();
		void ExportModPackage();
		// Interface language. English is the base; a catalogue in Lang/ translates it.
		std::string LoadPreferredLanguage() const;
		void SavePreferredLanguage(const std::string &code) const;
		// CyGPUInspectorApp.settings.json beside the executable: one object, each part of the
		// interface owning a key of it. Saving reads the file again, so no part erases another.
		static Json LoadPreferences();
		static void SavePreferences(const Json &root);
		void WriteTranslationTemplate();
		void DrawMcpPanel();
		void DrawDetailsPanel();
		void DrawStatusBar();

		void RefreshSessions();
		void ConnectTo(const SessionEntry &entry);
		void StartPreview(uint32_t resource_id);
		void StopPreview();
		void UpdatePreview();
		void RequestAnalysis(uint32_t shader_id);
		void RefreshDisassemblyLines();
		void RefreshFrameGraph(bool force);
		// Runtime shader control: both go through the same replacement mechanism.
		void HighlightShader(uint32_t shader_id);
		void InjectHlsl(uint32_t shader_id, const std::string &hlsl,
		                const char *origin = "edited HLSL", bool exportable = true,
		                const std::string &entry_point = "main");
		std::string ProfileOf(uint32_t shader_id) const;
		// The original byte code of a shader, as the game gave it. Empty when it is not known.
		std::vector<uint8_t> OriginalCode(uint32_t shader_id);

		// The model the panels read: a live session, or an opened capture. Null when neither.
		SessionModel *ActiveModel();
		const SessionModel *ActiveModel() const;
		// True when commands can still reach a game. A capture is read only by construction.
		bool IsLive() const { return m_client != nullptr && m_capture_model == nullptr; }

		void SaveCapture();
		void OpenCapture(const CaptureInfo &info);
		void CloseCapture();
		void RefreshCaptures();
		void CreateAnalysisSnapshot();
		// Answers one MCP tool call, on the UI thread, with the model lock held inside.
		Json HandleMcpRequest(const Json &request);

		// The AI assistant: its panel, and the AI Analysis tab. Defined in AssistantPanel.cpp.
		void DrawAssistantPanel();
		void DrawAssistantChat(bool ready);
		void DrawAiAnalysisTab();
		void UpdateAssistant(double delta_seconds);
		void ConnectAssistant();
		void LoadAssistantPreferences();
		void SaveAssistantPreferences();
		Json AssistantSelection() const;
		void RequestShaderJob(assistant::JobKind kind, uint32_t shader_id);
		bool GatherShaderData(uint32_t shader_id, Json &out, Sha256Digest &signature, std::string &error, bool &wait);
		void StoreJobOutcome(const assistant::JobOutcome &outcome);

		// The game profile and its persistent state. Defined in GameProfilePanel.cpp.
		std::filesystem::path GamesRoot();
		void UpdateGameProfile(double delta_seconds);
		void CloseGameProfile();
		void SaveProfileState();
		void ReapplyProfileState();
		void DrawGameProfilePanel();

		// HLSL: the coloured tab and the editor window. Defined in HlslEditorPanel.cpp.
		void DrawHlslTab(const ShaderAnalysis &analysis, bool has_analysis);
		void DrawHlslEditorWindow();
		void OpenHlslEditor(uint32_t shader_id, const std::string &text, const std::string &origin, bool open_window);
		bool HlslModified() const;
		void CompileEditorHlsl();
		void InjectEditorHlsl();
		void OpenHlslFile();
		void SaveHlslFile();

		// Searching shaders (section 46): what each shader did in the analysed frame, computed once
		// per frame and only when a query asks about targets, and the names and tags of the notes,
		// read once per signature.
		const std::unordered_map<uint32_t, ShaderFrameFacts> &ShaderFactsOf(const SessionModel &model,
		                                                                    const ShaderQuery &query);
		const ShaderNotesView *NotesViewOf(const Sha256Digest &signature);

		// Tags and annotations, kept per signature so they survive everything.
		void LoadNotesFor(uint32_t shader_id);
		void SaveNotes();
		void DrawNotesEditor(uint32_t shader_id);

		ID3D11Device *m_device = nullptr;
		ID3D11DeviceContext *m_context = nullptr;

		SessionDirectory m_directory;
		std::vector<SessionEntry> m_sessions;
		double m_since_refresh = 0.0;
		double m_since_throughput = 0.0;

		std::unique_ptr<SessionClient> m_client;

		std::unique_ptr<SessionModel> m_capture_model;
		CaptureInfo m_capture_info;
		std::vector<CaptureInfo> m_captures;
		std::unique_ptr<SessionModel> m_compare_model;
		CaptureInfo m_compare_info;
		CaptureDiff m_diff;
		bool m_has_diff = false;
		std::string m_capture_message;

		McpBridge m_mcp;

		ShaderNotes m_notes;
		uint32_t m_notes_shader = 0;
		Sha256Digest m_notes_signature;
		char m_note_text[512] = {};
		int m_note_kind = 0;

		uint32_t m_selected_shader = 0;
		uint32_t m_selected_resource = 0;
		uint32_t m_selected_event = 0;

		SharedTexture m_preview;
		uint32_t m_preview_resource = 0;
		uint32_t m_preview_mip = 0;
		uint32_t m_preview_slice = 0;
		PreviewStatus m_preview_status = PreviewStatus::ready;
		bool m_preview_fit = true;
		float m_preview_zoom = 1.0f;
		std::string m_preview_message;
		PreviewRenderer m_preview_renderer;
		PreviewSettings m_preview_settings;
		bool m_preview_renderer_ready = false;
		// The display pass runs once per interface frame; both places that show the image read
		// this, so they never disagree about what it looks like.
		ID3D11ShaderResourceView *m_preview_view_this_frame = nullptr;
		// The final image is shown by itself once per connection, unless the user stopped it.
		const void *m_preview_auto_client = nullptr;
		bool m_preview_frozen_sent = false;
		uint64_t m_preview_frozen_frame = 0;
		std::string m_preview_saved;
		// A deep capture finished and the add-on is holding the image of its last frame: the
		// image stays on that frame, whatever Follow says, until the user lets it go live again.
		bool m_preview_capture_hold = false;
		CaptureStage m_last_capture_stage = CaptureStage::idle;
		// The first frame of the last finished capture already acted on. Each finished capture is
		// recognised by that number rather than by watching the stage change: the records saying
		// "armed" and "finished" can both arrive between two interface frames, which hid the
		// transition, and a capture started by an agent over MCP never goes through the button.
		uint64_t m_handled_capture_frame = 0;
		int m_capture_save_countdown = 0;       // frames to wait for the held image to land
		std::string m_capture_image_path;
		// Every capture gets a folder of its own under Images/, for its final image and buffers.
		std::filesystem::path m_capture_folder;
		uint64_t m_capture_folder_first = 0;    // the capture it belongs to
		uint64_t m_images_tab_capture = 0;      // the capture the Images tab was last opened for
		CaptureBufferWriter m_buffer_writer;
		// A saved buffer shown in the Deep capture panel instead of the final image, read back
		// from its PNG: what is looked at is exactly what was written.
		int m_buffer_view_index = -1;
		ID3D11ShaderResourceView *m_buffer_view = nullptr;
		uint32_t m_buffer_view_width = 0;
		uint32_t m_buffer_view_height = 0;
		std::string m_buffer_view_error;
		// The timings the timeline shows while Follow is off: frozen with the rest of it.
		FrameTimings m_frozen_timings;
		// The adapter this application renders with, to say so when it is not the game's.
		std::string m_adapter_name;

		ShaderAnalysisService m_analysis;
		uint32_t m_disassembly_shader = 0;      // which shader m_disassembly_lines belongs to
		std::vector<std::string> m_disassembly_lines;
		char m_disassembly_filter[128] = {};
		bool m_disassembly_wrap = false;
		int m_selected_backend = 0;
		std::string m_inject_message;

		// The HLSL document of the HLSL tab and of the editor window, and the shader it belongs to.
		HlslEditor m_hlsl_editor;
		uint32_t m_hlsl_shader = 0;
		std::string m_hlsl_origin;            // "cygi-dxbc 0.1", "AI clean-up (model)", "file x.hlsl"
		std::string m_hlsl_file;
		std::string m_hlsl_status;
		std::vector<CompilerMessage> m_hlsl_messages;
		char m_hlsl_entry[64] = "main";
		char m_hlsl_profile[32] = {};
		bool m_hlsl_window = false;
		bool m_hlsl_window_focus = false;

		// The profile of the game being looked at (section 63), and its persistent state (section 38).
		GameProfile m_profile;
		const void *m_profile_owner = nullptr;   // the session or capture it was opened for
		bool m_profile_live = false;             // a live session: noted and counted, not only shown
		size_t m_profile_noted = 0;              // shaders of the model already noted
		uint32_t m_profile_new = 0;
		uint32_t m_profile_known = 0;
		uint32_t m_profile_known_code = 0;
		ModPackage m_profile_state;
		std::vector<bool> m_profile_applied;     // per entry of m_profile_state, this session
		bool m_profile_reapplying = false;
		double m_profile_since_reapply = 0.0;
		double m_profile_since_save = 0.0;
		std::string m_profile_message;
		char m_profile_filter[64] = {};
		std::unordered_map<uint32_t, ShaderFrameFacts> m_query_facts;
		const SessionModel *m_query_facts_model = nullptr;
		uint64_t m_query_facts_frame = UINT64_MAX;
		std::unordered_map<std::string, ShaderNotesView> m_notes_views;
		bool m_profile_focus = false;

		// The AI assistant, and what its panel and the AI Analysis tab keep between frames.
		assistant::Assistant m_assistant;
		int m_assistant_provider = 0;
		char m_assistant_model[160] = {};
		char m_assistant_endpoint[1024] = {};
		char m_assistant_key[512] = {};       // in memory only, never saved
		char m_assistant_input[4096] = {};
		uint64_t m_assistant_seen_version = 0;
		bool m_assistant_focus = false;
		bool m_assistant_refocus = false;
		// A shader job waits here until the disassembly and the decompilation it sends are ready.
		struct AiRequest
		{
			bool active = false;
			assistant::JobKind kind = assistant::JobKind::explanation;
			uint32_t shader_id = 0;
			double waited = 0.0;
			double since_try = 0.0;
		};
		AiRequest m_ai_request;
		assistant::Text m_ai_notice;
		HlslEditor m_ai_preview;              // read-only view of an AI clean-up
		int m_ai_preview_index = -1;

		// Deep capture controls and what came back.
		int m_deep_frames = 1;
		bool m_deep_bindings = true;
		bool m_deep_barriers = true;
		bool m_deep_per_draw_timing = true;
		bool m_deep_buffers = true;
		std::string m_deep_message;
		uint64_t m_deep_frame_shown = 0;
		bool m_timeline_follow = true;
		// The window of the timing view, in its own unit (milliseconds, or commands when nothing is
		// timed). Fitted to the frame until the user zooms or pans, and again on a double-click.
		FrameTimeline m_timeline;

		// The continuous view: every measured frame of the last few seconds, and where the view
		// onto them is. Its own state, so switching between the two views loses neither.
		FrameTrack m_track;
		const SessionModel *m_track_model = nullptr;
		bool m_timeline_continuous = false;
		uint64_t m_track_selected_frame = 0;
		int m_track_selected_pass = -1;          // position in that frame's passes
		bool m_track_follow = true;              // keep the newest frame at the right edge
		double m_track_view_start = 0.0;         // milliseconds on the track's axis
		double m_track_view_span = 0.0;
		bool m_track_dragged = false;
		uint64_t m_track_center_request = 0;     // a frame picked in the strip, to bring into view
		bool m_view_fit = true;
		double m_view_start = 0.0;
		double m_view_span = 0.0;
		bool m_view_dragged = false;

		// What has been changed in the connected game, and the metadata of the package it exports
		// to. Kept across a disconnection on purpose: the work outlives the session.
		ModRecorder m_mods;
		char m_mod_name[96] = {};
		char m_mod_author[64] = {};
		char m_mod_version[24] = "1.0";
		char m_mod_description[512] = {};
		char m_mod_directory[512] = {};
		std::string m_mod_message;

		FrameGraph m_graph;
		// What the successive rebuilds of m_graph said about each pass, for the model it was built on.
		PassHistory m_pass_history;
		const SessionModel *m_pass_history_model = nullptr;
		uint64_t m_graph_frame = 0;
		double m_since_graph = 0.0;
		bool m_graph_auto = true;
		uint32_t m_selected_pass = 0;
		char m_pass_rename[64] = {};

		char m_shader_filter[128] = {};
		char m_resource_filter[128] = {};
		bool m_only_render_targets = false;
		bool m_only_used_this_frame = false;
		// What the Frame events list leaves out. A frame is tens of thousands of commands and most
		// of them are binds and clears you are not looking for.
		bool m_events_draws_only = false;
		bool m_events_shader_only = false;
		bool m_events_pass_only = false;
		bool m_events_scroll_pending = false;
		bool m_layout_built = false;
		bool m_reset_layout = false;         // View > Reset the layout, applied next frame
		// Counts down over the first frames that follow building the default layout, while each
		// dock group is told which of its tabs to open on. It takes more than one frame: on the
		// frame a dock node is created, docking a window into it also claims its tab bar, so a
		// focus request made on that same frame is overwritten by the next window docked.
		int m_default_tabs_pending = 0;
		bool m_picking_tabs = false;           // this frame is one of those
		bool m_show_demo = false;
		uint32_t m_startup_shader = 0;
		bool m_startup_decompile = false;
		std::string m_code_tab_request;        // a tab of Shader code to select once: "HLSL", "AI Analysis"
		bool m_show_about = false;
		ID3D11ShaderResourceView *m_logo = nullptr;
		bool m_wants_exit = false;
		std::string m_status;
	};
}
