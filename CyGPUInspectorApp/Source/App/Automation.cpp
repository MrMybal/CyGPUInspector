// Semantic automation runs exclusively on the application's UI thread.
#include "Application.hpp"
#include <CyGPUInspectorCore/AutomationTools.hpp>
#include <CyGPUInspectorCore/Localization.hpp>
#include <CyGPUInspectorCore/Version.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <imgui_internal.h>
namespace cygi {
namespace {
using J = cymcp::Json;
const char *panels[] = {
    "Connections",  "Shaders",     "Resources",   "Frame events",
    "Preview",      "Shader code", "Frame graph", "Frame timeline",
    "Deep capture", "Captures",    "Mod export",  "Game profile",
    "AI assistant", "MCP",         "Details",     "Status"};
bool KnownPanel(const std::string &name) {
  for (auto p : panels)
    if (name == p)
      return true;
  return false;
}
std::filesystem::path ExportRoot() {
  auto p = CaptureArchive::DefaultRoot().parent_path() / "Automation";
  std::filesystem::create_directories(p);
  return p;
}
std::filesystem::path ExportPath(const std::string &prefix,
                                 const std::string &suffix) {
  return ExportRoot() / (prefix + "-" + cymcp::random_token(16) + suffix);
}
std::filesystem::path ImportPath(const std::string &name,
                                 const std::string &suffix) {
  auto p = std::filesystem::u8path(name);
  if (name.empty() || p.filename() != p || p.extension() != suffix)
    throw std::runtime_error(
        "Choose a matching file name from list_automation_files");
  auto root = std::filesystem::weakly_canonical(ExportRoot());
  auto full = std::filesystem::weakly_canonical(root / p);
  if (full.parent_path() != root || !std::filesystem::is_regular_file(full) ||
      std::filesystem::is_symlink(std::filesystem::symlink_status(root / p)))
    throw std::runtime_error("File is outside the automation export directory");
  return full;
}
std::string ReadText(const std::filesystem::path &p) {
  if (std::filesystem::file_size(p) > 1048576)
    throw std::runtime_error("Text file exceeds one MiB");
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot read file");
  return {std::istreambuf_iterator<char>(f), {}};
}
void WriteNew(const std::filesystem::path &p, const std::string &text) {
  if (std::filesystem::exists(p))
    throw std::runtime_error("Refusing to overwrite an existing file");
  auto f = cymcp::open_private_output_file(p);
  if (std::fwrite(text.data(), 1, text.size(), f.get()) != text.size())
    throw std::runtime_error("Cannot write complete export");
}
template <size_t N> void Copy(char (&buffer)[N], const std::string &value) {
  if (value.size() >= N)
    throw std::runtime_error("Value exceeds field capacity");
  std::snprintf(buffer, N, "%s", value.c_str());
}
} // namespace
bool Application::HandleAutomation(const Json &request, Json &response) {
  const std::string tool = request["tool"].AsString();
  if (!FindAutomationTool(tool))
    return false;
  auto finish = [&](J result) {
    Json::Parse(J{{"ok", true}, {"result", result}}.dump(), response);
    return true;
  };
  try {
    const J a =
        cymcp::parse_json(request["arguments"].Write(-1), 2 * 1024 * 1024);
    auto model = [&]() -> SessionModel & {
      auto *m = ActiveModel();
      if (!m)
        throw std::runtime_error("No active session or capture");
      return *m;
    };
    auto shader = [&](uint32_t id) {
      auto &m = model();
      std::lock_guard<std::mutex> g(m.Mutex());
      if (!m.ShaderById(id))
        throw std::runtime_error("Unknown shader id");
    };
    auto backup_layout = [&]() {
      size_t size = 0;
      const char *ini = ImGui::SaveIniSettingsToMemory(&size);
      auto p = ExportPath("layout", ".layout");
      WriteNew(p, std::string(ini, size));
      return cymcp::path_utf8(p);
    };
    auto backup_editor = [&]() {
      auto p = ExportPath("draft", ".hlsl");
      WriteNew(p, m_hlsl_editor.GetText());
      return cymcp::path_utf8(p);
    };
    J themes = J::array(), languages = J::array();
    if (tool == "get_application_state") {
      for (const auto &theme : cyui::builtInThemes())
        themes.push_back({{"key", theme.key}, {"name", theme.name}});
      for (const auto &language : i18n::Languages())
        languages.push_back({{"code", language.code}, {"name", language.name}});
    }
    if (tool == "get_application_state")
      return finish(
          {{"version", kVersionString},
           {"themes", themes},
           {"languages", languages},
           {"automation_files",
            cymcp::path_utf8(CaptureArchive::DefaultRoot().parent_path() /
                             "Automation")},
           {"connected", m_client != nullptr},
           {"viewing_capture", m_capture_model != nullptr},
           {"workspace", m_workspace},
           {"theme", m_theme},
           {"comfortable", m_density == 1},
           {"scale", m_ui_scale},
           {"language", i18n::CurrentLanguage()},
           {"shader_id", m_selected_shader},
           {"resource_id", m_selected_resource},
           {"event_index", m_selected_event},
           {"pass_id", m_selected_pass},
           {"follow", m_timeline_follow},
           {"editor_modified", HlslModified()},
           {"shader_filter", m_shader_filter},
           {"resource_filter", m_resource_filter},
           {"external_permission", static_cast<int>(m_mcp.Permission())}});
    if (tool == "list_sessions") {
      RefreshSessions();
      J rows = J::array();
      for (const auto &s : m_sessions)
        rows.push_back({{"process_id", s.process_id},
                        {"device_index", s.device_index},
                        {"process_name", s.process_name},
                        {"api", GraphicsApiName(s.api)},
                        {"fresh", s.IsFresh()}});
      return finish({{"sessions", rows}});
    }
    if (tool == "connect_session") {
      RefreshSessions();
      std::vector<SessionEntry> matches;
      for (const auto &s : m_sessions)
        if (s.process_id == a.at("process_id").get<uint32_t>() &&
            (!a.contains("device_index") ||
             s.device_index == a["device_index"].get<uint32_t>()) &&
            s.IsFresh())
          matches.push_back(s);
      if (matches.size() != 1)
        throw std::runtime_error(
            "Select exactly one fresh process/device from list_sessions");
      ConnectTo(matches[0]);
      if (!m_client)
        throw std::runtime_error(m_status);
      CloseCapture();
      return finish({{"message", m_status}});
    }
    if (tool == "disconnect_session") {
      m_client.reset();
      StopPreview();
      m_status = "Disconnected";
      return finish({{"connected", false}});
    }
    if (tool == "open_capture") {
      RefreshCaptures();
      for (const auto &c : m_captures)
        if (c.path.filename().u8string() ==
            std::filesystem::u8path(a.at("name").get<std::string>())
                .u8string()) {
          OpenCapture(c);
          if (!m_capture_model || m_capture_info.path != c.path)
            throw std::runtime_error(m_capture_message);
          return finish({{"capture", c.path.filename().string()}});
        }
      throw std::runtime_error("Choose a capture name from list_captures");
    }
    if (tool == "close_capture") {
      CloseCapture();
      return finish({{"viewing_capture", false}});
    }
    if (tool == "set_tracking_level") {
      const char *levels[] = {"idle", "tracking", "pass_timing", "capture",
                              "full_draw_timing"};
      int level = 0;
      while (a["level"] != levels[level])
        ++level;
      std::string message;
      if (!SetTrackingLevel(static_cast<TrackingLevel>(level), message))
        throw std::runtime_error(message);
      return finish({{"level", a["level"]}, {"message", message}});
    }
    if (tool == "request_full_sync") {
      if (!IsLive() || !m_client->RequestFullSync())
        throw std::runtime_error("Live producer refused synchronization");
      return finish({{"requested", true}});
    }
    if (tool == "set_follow_frame") {
      m_timeline_follow = a["follow"].get<bool>();
      m_graph_auto = m_timeline_follow;
      return finish({{"follow", m_timeline_follow}});
    }
    if (tool == "refresh_frame_graph") {
      model();
      RefreshFrameGraph(true);
      return finish({{"passes", m_graph.Passes().size()}});
    }
    if (tool == "select_shader") {
      auto id = a["shader_id"].get<uint32_t>();
      shader(id);
      m_selected_shader = id;
      RequestAnalysis(id);
      FocusPanel("Shader code");
      return finish({{"shader_id", id}});
    }
    if (tool == "select_resource") {
      auto id = a["resource_id"].get<uint32_t>();
      auto &m = model();
      {
        std::lock_guard<std::mutex> g(m.Mutex());
        if (!m.ResourceById(id))
          throw std::runtime_error("Unknown resource id");
      }
      m_selected_resource = id;
      FocusPanel("Resources");
      return finish({{"resource_id", id}});
    }
    if (tool == "select_event") {
      auto id = a["event_index"].get<uint32_t>();
      auto &m = model();
      {
        std::lock_guard<std::mutex> g(m.Mutex());
        if (id >= m.LastFrame().events.size())
          throw std::runtime_error("Unknown event index");
      }
      SelectEvent(id);
      FocusPanel("Frame events");
      return finish({{"event_index", id}});
    }
    if (tool == "select_pass" || tool == "rename_pass") {
      model();
      RefreshFrameGraph(true);
      auto id = a["pass_id"].get<uint32_t>();
      const auto *p = m_graph.PassById(id);
      if (!p)
        throw std::runtime_error("Unknown pass id");
      if (tool == "rename_pass") {
        auto &m = model();
        std::lock_guard<std::mutex> g(m.Mutex());
        m_pass_history.SetUserName(PassHistory::KeyOf(*p, m), a["name"]);
        m_graph.RenamePass(id, a["name"]);
      }
      m_selected_pass = id;
      FocusPanel("Frame graph");
      return finish({{"pass_id", id}, {"name", m_graph.PassById(id)->name}});
    }
    if (tool == "set_filters") {
      if (a.contains("shaders"))
        Copy(m_shader_filter, a["shaders"]);
      if (a.contains("resources"))
        Copy(m_resource_filter, a["resources"]);
      if (a.contains("render_targets_only"))
        m_only_render_targets = a["render_targets_only"];
      if (a.contains("used_this_frame"))
        m_only_used_this_frame = a["used_this_frame"];
      if (a.contains("draws_only"))
        m_events_draws_only = a["draws_only"];
      if (a.contains("selected_shader_only"))
        m_events_shader_only = a["selected_shader_only"];
      if (a.contains("selected_pass_only"))
        m_events_pass_only = a["selected_pass_only"];
      return finish({{"updated", true}});
    }
    if (tool == "set_workspace") {
      const char *names[] = {"inspect", "captures", "preview", "assistant"};
      int index = 0;
      while (a["workspace"] != names[index])
        ++index;
      FocusWorkspace(index);
      return finish({{"workspace", a["workspace"]}});
    }
    if (tool == "focus_panel") {
      const auto name = a["panel"].get<std::string>();
      if (!KnownPanel(name))
        throw std::runtime_error("Unknown panel; use get_panel_layout");
      FocusPanel(name.c_str());
      return finish({{"panel", name}});
    }
    if (tool == "get_panel_layout") {
      J rows = J::array();
      for (auto name : panels) {
        J row = {{"panel", name}, {"created", false}};
        if (const auto *w = ImGui::FindWindowByName(i18n::TrId(name))) {
          row["created"] = true;
          row["dock_id"] = w->DockId;
          row["x"] = w->Pos.x;
          row["y"] = w->Pos.y;
          row["width"] = w->Size.x;
          row["height"] = w->Size.y;
        }
        rows.push_back(row);
      }
      return finish({{"panels", rows}});
    }
    if (tool == "reset_layout") {
      auto backup = backup_layout();
      m_reset_layout = true;
      return finish({{"scheduled", true}, {"backup", backup}});
    }
    if (tool == "restore_layout") {
      auto p = ImportPath(a["file"], ".layout");
      auto text = ReadText(p);
      auto backup = backup_layout();
      ImGui::LoadIniSettingsFromMemory(text.data(), text.size());
      return finish({{"restored", a["file"]}, {"backup", backup}});
    }
    if (tool == "dock_panel") {
      auto name = a["panel"].get<std::string>(),
           target = a["target"].get<std::string>();
      if (!KnownPanel(name) || !KnownPanel(target) || name == target)
        throw std::runtime_error("Select two distinct known panels");
      auto *window = ImGui::FindWindowByName(i18n::TrId(target.c_str()));
      if (!window || !window->DockId)
        throw std::runtime_error("Target panel is not currently docked");
      auto backup = backup_layout();
      ImGuiID dock = window->DockId;
      const auto side = a["side"].get<std::string>();
      if (side != "tab") {
        auto dir = side == "left"    ? ImGuiDir_Left
                   : side == "right" ? ImGuiDir_Right
                   : side == "up"    ? ImGuiDir_Up
                                     : ImGuiDir_Down;
        dock = ImGui::DockBuilderSplitNode(dock, dir, a.value("ratio", .5f),
                                           nullptr, nullptr);
      }
      ImGui::DockBuilderDockWindow(i18n::TrId(name.c_str()), dock);
      ImGui::MarkIniSettingsDirty();
      return finish({{"docked", name}, {"target", target}, {"backup", backup}});
    }
    if (tool == "set_appearance") {
      auto theme = a.value("theme", m_theme);
      bool known = false;
      for (const auto &t : cyui::builtInThemes())
        known |= t.key == theme;
      if (!known)
        throw std::runtime_error("Unknown built-in theme");
      SetAppearance(theme, a.value("comfortable", m_density == 1) ? 1 : 0,
                    a.value("scale", m_ui_scale));
      return finish({{"theme", m_theme},
                     {"comfortable", m_density == 1},
                     {"scale", m_ui_scale}});
    }
    if (tool == "set_language") {
      if (!i18n::SetLanguage(a["language"]))
        throw std::runtime_error("Language is not installed");
      SavePreferredLanguage(a["language"]);
      SyncCyuiLocale();
      return finish({{"language", i18n::CurrentLanguage()}});
    }
    if (tool == "open_help") {
      const auto topic = a["topic"].get<std::string>();
      if (topic.empty() ||
          topic.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") !=
              std::string::npos)
        throw std::runtime_error("Invalid documentation topic id");
      OpenHelp(topic, a.value("language", i18n::CurrentLanguage()));
      return finish({{"opened", topic}});
    }
    if (tool == "open_about") {
      m_show_about = true;
      return finish({{"visible", true}});
    }
    if (tool == "get_preview_settings")
      return finish({{"resource_id", m_preview_resource},
                     {"mode", static_cast<uint32_t>(m_preview_settings.mode)},
                     {"min", m_preview_settings.range_min},
                     {"max", m_preview_settings.range_max},
                     {"near", m_preview_settings.near_plane},
                     {"far", m_preview_settings.far_plane},
                     {"reverse_z", m_preview_settings.reverse_z},
                     {"checkerboard", m_preview_settings.checkerboard_alpha},
                     {"mip", m_preview_mip},
                     {"slice", m_preview_slice},
                     {"fit", m_preview_fit},
                     {"zoom", m_preview_zoom}});
    if (tool == "set_preview_settings") {
      auto next = m_preview_settings;
      next.mode = static_cast<PreviewChannelMode>(
          a.value("mode", static_cast<uint32_t>(next.mode)));
      next.range_min = a.value("min", next.range_min);
      next.range_max = a.value("max", next.range_max);
      next.near_plane = a.value("near", next.near_plane);
      next.far_plane = a.value("far", next.far_plane);
      next.reverse_z = a.value("reverse_z", next.reverse_z);
      next.checkerboard_alpha =
          a.value("checkerboard", next.checkerboard_alpha);
      if (next.range_max <= next.range_min || next.far_plane <= next.near_plane)
        throw std::runtime_error("Display ranges must be increasing");
      m_preview_settings = next;
      m_preview_mip = a.value("mip", m_preview_mip);
      m_preview_slice = a.value("slice", m_preview_slice);
      m_preview_fit = a.value("fit", m_preview_fit);
      m_preview_zoom = a.value("zoom", m_preview_zoom);
      if (a.contains("mip") || a.contains("slice"))
        StartPreview(m_preview_resource);
      return finish({{"updated", true}});
    }
    if (tool == "start_preview") {
      if (!IsLive())
        throw std::runtime_error("No live preview available");
      auto id = a["resource_id"].get<uint32_t>();
      if (id) {
        auto &m = model();
        std::lock_guard<std::mutex> g(m.Mutex());
        if (!m.ResourceById(id))
          throw std::runtime_error("Unknown resource");
      }
      StartPreview(id ? id : kPreviewFinalImage);
      return finish({{"requested", true},
                     {"resource_id", m_preview_resource},
                     {"message", m_preview_message}});
    }
    if (tool == "set_preview_frozen") {
      if (!IsLive() || !m_client->SetPreviewFrozen(a["frozen"]))
        throw std::runtime_error("Producer refused preview freeze");
      return finish({{"frozen", a["frozen"]}});
    }
    if (tool == "stop_preview") {
      StopPreview();
      return finish({{"stopped", true}});
    }
    if (tool == "export_preview") {
      auto p = ExportPath("preview", ".png");
      std::string error;
      if (!m_preview_renderer.SavePng(p.wstring(), error))
        throw std::runtime_error(error);
      return finish({{"file", cymcp::path_utf8(p)}});
    }
    if (tool == "get_shader_notes" || tool == "set_shader_name" ||
        tool == "set_shader_tag" || tool == "add_shader_annotation" ||
        tool == "edit_shader_annotation" ||
        tool == "remove_shader_annotation" || tool == "restore_shader_notes") {
      auto id = a["shader_id"].get<uint32_t>();
      shader(id);
      LoadNotesFor(id);
      std::string note_backup;
      if (tool != "get_shader_notes") {
        J archive = {{"shader_signature", m_notes_signature.ToHex()},
                     {"name", m_notes.user_name},
                     {"tags", J::array()},
                     {"annotations", J::array()}};
        for (auto tag : m_notes.tags)
          archive["tags"].push_back(ShaderTagName(tag));
        for (const auto &n : m_notes.annotations)
          archive["annotations"].push_back(
              {{"source", static_cast<int>(n.source)},
               {"kind", static_cast<int>(n.kind)},
               {"text", n.text},
               {"author", n.author},
               {"prompt_version", n.prompt_version},
               {"created", n.created},
               {"confidence", n.confidence}});
        auto file = ExportPath("notes", ".notes");
        WriteNew(file, archive.dump(2));
        note_backup = cymcp::path_utf8(file);
      }
      if (tool == "restore_shader_notes") {
        const auto old =
            cymcp::parse_json(ReadText(ImportPath(a["file"], ".notes")));
        if (old.at("shader_signature") != m_notes_signature.ToHex())
          throw std::runtime_error("Notes backup belongs to another shader");
        ShaderNotes restored;
        restored.user_name = old.at("name");
        for (const auto &name : old.at("tags")) {
          ShaderTag tag;
          if (!ShaderTagFromName(name, tag))
            throw std::runtime_error("Invalid backup tag");
          restored.tags.push_back(tag);
        }
        for (const auto &row : old.at("annotations")) {
          Annotation n;
          int source = row.at("source"), kind = row.at("kind");
          if (source < 0 || source > 2 || kind < 0 || kind > 3)
            throw std::runtime_error("Invalid annotation origin");
          n.source = static_cast<AnnotationSource>(source);
          n.kind = static_cast<AnnotationKind>(kind);
          n.text = row.at("text");
          n.author = row.at("author");
          n.prompt_version = row.at("prompt_version");
          n.created = row.at("created");
          n.confidence = row.at("confidence");
          restored.annotations.push_back(n);
        }
        m_notes = std::move(restored);
      }
      if (tool == "edit_shader_annotation" ||
          tool == "remove_shader_annotation") {
        const auto index = a["index"].get<size_t>();
        if (index >= m_notes.annotations.size() ||
            m_notes.annotations[index].text !=
                a["expected_text"].get<std::string>())
          throw std::runtime_error(
              "Annotation changed; reread get_shader_notes");
        if (tool == "remove_shader_annotation")
          m_notes.annotations.erase(m_notes.annotations.begin() + index);
        else {
          auto &n = m_notes.annotations[index];
          n.text = a["text"];
          n.source = AnnotationSource::ai;
          n.author = "Application automation";
        }
      }
      if (tool == "set_shader_name")
        m_notes.user_name = a["name"];
      if (tool == "set_shader_tag") {
        ShaderTag tag;
        if (!ShaderTagFromName(a["tag"], tag))
          throw std::runtime_error("Unknown shader tag");
        m_notes.SetTag(tag, a["enabled"]);
      }
      if (tool == "add_shader_annotation") {
        Annotation note;
        note.source = AnnotationSource::ai;
        note.text = a["text"];
        note.author = "Application automation";
        note.confidence = a.value("confidence", 0.f) / 100.f;
        m_notes.annotations.push_back(note);
      }
      if (tool != "get_shader_notes") {
        if (!SaveShaderNotes(m_analysis.Store(), m_notes_signature, m_notes))
          throw std::runtime_error("Could not persist shader notes");
        m_notes_views.erase(m_notes_signature.ToHex());
      }
      J tags = J::array(), notes = J::array();
      for (auto t : m_notes.tags)
        tags.push_back(ShaderTagName(t));
      for (const auto &n : m_notes.annotations)
        notes.push_back({{"source", AnnotationSourceName(n.source)},
                         {"kind", AnnotationKindName(n.kind)},
                         {"text", n.text},
                         {"author", n.author},
                         {"confidence", n.confidence}});
      return finish({{"shader_id", id},
                     {"name", m_notes.user_name},
                     {"tags", tags},
                     {"annotations", notes},
                     {"backup", note_backup}});
    }
    if (tool == "get_editor_state")
      return finish({{"hlsl", m_hlsl_editor.GetText()},
                     {"sha256", cymcp::sha256(m_hlsl_editor.GetText())},
                     {"shader_id", m_hlsl_shader},
                     {"entry", m_hlsl_entry},
                     {"profile", m_hlsl_profile},
                     {"modified", HlslModified()},
                     {"status", m_hlsl_status}});
    if (tool == "set_editor_text" || tool == "import_editor") {
      if (a["expected_sha256"] != cymcp::sha256(m_hlsl_editor.GetText()))
        throw std::runtime_error(
            "Editor changed; reread get_editor_state before editing");
      auto text = tool == "import_editor"
                      ? ReadText(ImportPath(a["file"], ".hlsl"))
                      : a["hlsl"].get<std::string>();
      auto id = a.value("shader_id", m_hlsl_shader);
      if (id)
        shader(id);
      auto backup = backup_editor();
      m_hlsl_editor.ReplaceText(text);
      m_hlsl_shader = id;
      m_hlsl_origin = "Automation";
      m_hlsl_messages.clear();
      m_hlsl_status.clear();
      m_hlsl_window = true;
      m_hlsl_window_focus = true;
      if (id && !a.contains("profile"))
        Copy(m_hlsl_profile, ProfileOf(id));
      if (a.contains("profile"))
        Copy(m_hlsl_profile, a["profile"]);
      if (a.contains("entry"))
        Copy(m_hlsl_entry, a["entry"]);
      return finish({{"sha256", cymcp::sha256(m_hlsl_editor.GetText())},
                     {"backup", backup}});
    }
    if (tool == "editor_undo" || tool == "editor_redo") {
      if (tool == "editor_undo") {
        if (!m_hlsl_editor.CanUndo())
          throw std::runtime_error("No editor undo available");
        m_hlsl_editor.Undo();
      } else {
        if (!m_hlsl_editor.CanRedo())
          throw std::runtime_error("No editor redo available");
        m_hlsl_editor.Redo();
      }
      return finish({{"sha256", cymcp::sha256(m_hlsl_editor.GetText())}});
    }
    if (tool == "set_editor_view") {
      if (a.contains("wrap"))
        m_hlsl_editor.SetWordWrap(a["wrap"]);
      if (a.contains("whitespace"))
        m_hlsl_editor.SetShowWhitespace(a["whitespace"]);
      if (a.contains("minimap"))
        m_hlsl_editor.SetShowMiniMap(a["minimap"]);
      if (a.contains("line"))
        m_hlsl_editor.GoToLine(a["line"]);
      if (a.value("find", false))
        m_hlsl_editor.OpenFind();
      return finish({{"updated", true}});
    }
    if (tool == "configure_assistant") {
      if (m_assistant.Wanted() || m_assistant.IsPending() ||
          m_assistant.IsJobRunning())
        throw std::runtime_error("Disconnect and finish pending work before "
                                 "changing provider preferences");
      auto &c = m_assistant.Config();
      const auto endpoint = a.value("endpoint", c.endpoint);
      if (!endpoint.empty())
        assistant::ValidateEndpoint(endpoint);
      const auto kind = static_cast<assistant::ProviderKind>(
          a.value("provider", static_cast<int>(c.kind)));
      // Never forward an existing credential to another service selected by a
      // tool.
      if (kind != c.kind || endpoint != c.endpoint) {
        c.key.clear();
        m_assistant_key[0] = '\0';
      }
      c.kind = kind;
      c.model = a.value("model", c.model);
      c.endpoint = endpoint;
      c.deferred_tools = a.value("deferred_tools", c.deferred_tools);
      c.context_bytes = a.value("context_bytes", c.context_bytes);
      c.remember_history = a.value("remember_history", c.remember_history);
      m_assistant_provider = static_cast<int>(c.kind);
      Copy(m_assistant_model, c.model);
      Copy(m_assistant_endpoint, c.endpoint);
      SaveAssistantPreferences();
      return finish({{"configured", true},
                     {"provider", m_assistant_provider},
                     {"model", c.model}});
    }
    if (tool == "connect_assistant") {
      if (m_assistant.Wanted())
        throw std::runtime_error(
            "Assistant is already connected or connecting");
      ConnectAssistant();
      return finish({{"connecting", true}});
    }
    if (tool == "disconnect_assistant") {
      m_assistant.Disconnect();
      return finish({{"disconnected", true}});
    }
    if (tool == "cancel_assistant_request") {
      m_assistant.CancelRequest();
      return finish({{"cancelled", true}});
    }
    if (tool == "export_conversation" || tool == "new_conversation") {
      if (tool == "new_conversation" && m_assistant.IsPending())
        throw std::runtime_error("Cancel active chat before resetting it");
      J rows = J::array();
      for (const auto &row : m_assistant.Conversation())
        rows.push_back({{"role", row.role},
                        {"content", row.content},
                        {"notice", assistant::English(row.notice)}});
      auto file = ExportPath("conversation", ".json");
      WriteNew(file, rows.dump(2));
      if (tool == "new_conversation" && !m_assistant.NewConversation())
        throw std::runtime_error(
            "Previous conversation is still stopping; retry when idle");
      return finish({{"file", cymcp::path_utf8(file)},
                     {"reset", tool == "new_conversation"}});
    }
    if (tool == "compile_editor") {
      auto result = CompileHlsl(m_hlsl_editor.GetText(), m_hlsl_entry,
                                m_hlsl_profile, "editor.hlsl");
      m_hlsl_messages = m_hlsl_editor.MarkCompilerMessages(
          result.messages.empty() ? result.error : result.messages);
      m_hlsl_status = result.ok ? "Compiled" : "Compilation failed";
      return finish({{"compiled", result.ok},
                     {"diagnostics", result.messages},
                     {"error", result.error},
                     {"bytes", result.byte_code.size()}});
    }
    if (tool == "inject_editor") {
      if (!IsLive() || !m_hlsl_shader)
        throw std::runtime_error("Editor is not assigned to a live shader");
      m_inject_message.clear();
      InjectEditorHlsl();
      if (m_inject_message.rfind("injected", 0) != 0)
        throw std::runtime_error(m_inject_message.empty() ? m_hlsl_status
                                                          : m_inject_message);
      return finish({{"message", m_inject_message}});
    }
    if (tool == "export_editor") {
      auto p = ExportPath("editor", ".hlsl");
      WriteNew(p, m_hlsl_editor.GetText());
      return finish({{"file", cymcp::path_utf8(p)}});
    }
    if (tool == "list_automation_files") {
      J files = J::array();
      for (const auto &e : std::filesystem::directory_iterator(ExportRoot()))
        if (e.is_regular_file() && !e.is_symlink())
          files.push_back({{"file", e.path().filename().string()},
                           {"bytes", e.file_size()}});
      return finish({{"files", files}});
    }
    if (tool == "get_mod_package") {
      J rows = J::array();
      for (const auto &change : m_mods.Mods())
        rows.push_back({{"shader_id", change.shader_id},
                        {"included", change.include},
                        {"origin", change.origin}});
      return finish({{"changes", rows}, {"included", m_mods.CountIncluded()}});
    }
    if (tool == "set_mod_included") {
      for (auto &change : m_mods.Mods())
        if (change.shader_id == a["shader_id"].get<uint32_t>()) {
          change.include = a["included"];
          return finish({{"updated", true}});
        }
      throw std::runtime_error("Shader has no recorded modification");
    }
    if (tool == "export_mod_package") {
      auto &m = model();
      ModPackage package;
      package.name = a["name"];
      package.author = a.value("author", "");
      package.version = a.value("version", "1.0");
      package.description = a.value("description", "");
      std::string error;
      if (!m_mods.BuildPackage(m, package, error))
        throw std::runtime_error(error);
      auto p = ExportPath("mod", ".cygimod");
      if (!package.Save(cymcp::path_utf8(p), error))
        throw std::runtime_error(error);
      return finish({{"directory", cymcp::path_utf8(p)},
                     {"replacements", package.CountOf(ModAction::replace)},
                     {"disabled", package.CountOf(ModAction::disable)}});
    }
    if (tool == "save_game_profile" || tool == "reapply_game_profile") {
      if (!IsLive() || !m_profile.IsOpen())
        throw std::runtime_error("No live game profile");
      if (tool == "save_game_profile")
        SaveProfileState();
      else {
        m_profile_applied.assign(m_profile_state.entries.size(), false);
        m_profile_reapplying = true;
        m_profile_since_reapply = 1.0;
        ReapplyProfileState();
      }
      return finish({{"requested", true}, {"message", m_profile_message}});
    }
    if (tool == "set_timing_focus") {
      if (!IsLive())
        throw std::runtime_error("No live timing producer");
      const char *names[] = {"none", "shader", "draw", "resource"};
      int kind = 0;
      while (a["kind"] != names[kind])
        ++kind;
      auto id = a["id"].get<uint32_t>();
      if (kind == 1)
        shader(id);
      SetTimingFocus(static_cast<TimingFocusKind>(kind), id);
      if (!m_timing_focus_message.empty())
        throw std::runtime_error(m_timing_focus_message);
      return finish({{"kind", a["kind"]}, {"id", m_timing_focus_id}});
    }
    if (tool == "set_timeline_view") {
      m_timeline_continuous = a.value("continuous", m_timeline_continuous);
      m_track_follow = a.value("follow", m_track_follow);
      m_view_start = a.value("start", m_view_start);
      m_view_span = a.value("span", m_view_span);
      m_track_view_start = m_view_start;
      m_track_view_span = m_view_span;
      if (a.contains("span"))
        m_view_fit = false;
      return finish({{"continuous", m_timeline_continuous},
                     {"follow", m_track_follow},
                     {"start", m_view_start},
                     {"span", m_view_span}});
    }
    if (tool == "get_assistant_conversation") {
      J rows = J::array();
      for (const auto &row : m_assistant.Conversation())
        rows.push_back({{"role", row.role},
                        {"content", row.content},
                        {"notice", assistant::English(row.notice)}});
      return finish({{"messages", rows},
                     {"pending", m_assistant.IsPending()},
                     {"progress", m_assistant.Progress()}});
    }
    if (tool == "send_assistant_message") {
      if (!m_assistant.Send(a["text"], AssistantSelection(),
                            i18n::CurrentLanguage() == "fr" ? "French"
                                                            : "English",
                            i18n::CurrentLanguage()))
        throw std::runtime_error(
            "Assistant is disconnected, busy, or the message is empty");
      return finish({{"accepted", true}});
    }

    if (tool == "export_analysis_snapshot") {
      model();
      CreateAnalysisSnapshot();
      if (m_capture_message.rfind("snapshot written", 0) != 0)
        throw std::runtime_error(m_capture_message);
      return finish({{"message", m_capture_message}});
    }
    if (tool == "get_timeline_history") {
      UpdateFrameTrack();
      J rows = J::array();
      for (const auto &f : m_track.Frames())
        rows.push_back({{"frame", f.index},
                        {"begin", f.begin},
                        {"end", f.end},
                        {"passes", f.passes.size()},
                        {"draws", f.draw_count},
                        {"own_structure", f.own_structure}});
      return finish(
          {{"frames", rows}, {"selected_frame", m_track_selected_frame}});
    }
    if (tool == "select_history_frame") {
      UpdateFrameTrack();
      const auto index = a["frame"].get<uint64_t>();
      const auto *f = m_track.FrameByIndex(index);
      if (!f)
        throw std::runtime_error("Timing frame is no longer retained");
      auto pass = a.value("pass_index", 0u);
      if (a.contains("pass_index") && pass >= f->passes.size())
        throw std::runtime_error("Unknown timing pass index");
      m_track_selected_frame = index;
      m_track_selected_pass =
          a.contains("pass_index") ? static_cast<int>(pass) : -1;
      m_timeline_continuous = true;
      m_track_follow = false;
      m_track_center_request = index;
      FocusPanel("Frame timeline");
      return finish({{"frame", index}});
    }
    if (tool == "show_capture_buffer") {
      auto items = m_buffer_writer.Items();
      auto index = a["index"].get<size_t>();
      if (index >= items.size())
        throw std::runtime_error("Unknown captured buffer");
      auto file = m_buffer_writer.Folder() / (items[index].file_stem + ".png");
      if (!std::filesystem::is_regular_file(file))
        throw std::runtime_error("Buffer PNG is not ready");
      ShowBufferFile(static_cast<int>(index), file);
      if (!m_buffer_view_error.empty())
        throw std::runtime_error(m_buffer_view_error);
      FocusPanel("Deep capture");
      return finish({{"index", index}, {"file", cymcp::path_utf8(file)}});
    }
    if (tool == "set_profile_reapply") {
      if (!m_profile.IsOpen())
        throw std::runtime_error("No game profile");
      m_profile.SetReapplyOnConnect(a["enabled"]);
      return finish({{"enabled", m_profile.ReapplyOnConnect()}});
    }
    if (tool == "clear_saved_profile_state") {
      if (!m_profile.IsOpen())
        throw std::runtime_error("No game profile");
      auto file = ExportPath("profile-backup", ".cygimod");
      std::string error;
      if (!m_profile_state.Empty() &&
          !m_profile_state.Save(cymcp::path_utf8(file), error))
        throw std::runtime_error(error);
      if (!m_profile.ClearState(error))
        throw std::runtime_error(error);
      m_profile_state = ModPackage();
      m_profile_applied.clear();
      m_profile_reapplying = false;
      return finish(
          {{"cleared", true},
           {"backup", std::filesystem::exists(file) ? cymcp::path_utf8(file)
                                                    : std::string()}});
    }
    if (tool == "set_profile_filter") {
      Copy(m_profile_filter, a["query"]);
      return finish({{"query", m_profile_filter}});
    }
    if (tool == "get_diagnostics")
      return finish({{"status", m_status},
                     {"capture", m_capture_message},
                     {"mod", m_mod_message},
                     {"compiler", m_hlsl_status},
                     {"profile", m_profile_message},
                     {"deep_capture", m_deep_message},
                     {"preview", m_preview_message}});
    if (tool == "clear_mod_recording") {
      if (m_mods.Empty())
        return finish({{"cleared", true}});
      auto copy = m_mods;
      for (auto &mod : copy.Mods())
        mod.include = true;
      ModPackage package;
      package.name = "Recorded changes backup";
      std::string error;
      if (!copy.BuildPackage(model(), package, error))
        throw std::runtime_error(error);
      auto file = ExportPath("mods-backup", ".cygimod");
      if (!package.Save(cymcp::path_utf8(file), error))
        throw std::runtime_error(error);
      m_mods.Clear();
      return finish({{"cleared", true}, {"backup", cymcp::path_utf8(file)}});
    }
    if (tool == "request_exit") {
      if (!m_mods.Empty())
        throw std::runtime_error("Export or clear recorded modifications with "
                                 "a backup before exiting");
      auto editor = backup_editor();
      J messages = J::array();
      for (const auto &row : m_assistant.Conversation())
        messages.push_back({{"role", row.role},
                            {"content", row.content},
                            {"notice", assistant::English(row.notice)}});
      auto history = ExportPath("conversation-before-exit", ".json");
      WriteNew(history, messages.dump(2));
      m_wants_exit = true;
      return finish({{"closing", true},
                     {"draft_backup", editor},
                     {"conversation_backup", cymcp::path_utf8(history)}});
    }
    if (tool == "get_assistant_status")
      return finish(
          {{"ready", m_assistant.IsReady()},
           {"provider", assistant::ProviderName(m_assistant.Config().kind)},
           {"model", m_assistant.Config().model},
           {"pending", m_assistant.IsPending()},
           {"job_running", m_assistant.IsJobRunning()},
           {"job_shader", m_assistant.JobShader()},
           {"mode", static_cast<int>(m_assistant.Mode())}});
    if (tool == "cancel_shader_job") {
      m_ai_request.active = false;
      m_assistant.CancelJob();
      return finish({{"cancelled", true}});
    }
    if (tool == "request_shader_job") {
      if (m_assistant.IsPending() || m_assistant.IsJobRunning() ||
          !m_assistant.IsReady())
        throw std::runtime_error("Assistant is busy or disconnected; schedule "
                                 "a shader job outside an active chat turn");
      auto id = a["shader_id"].get<uint32_t>();
      shader(id);
      auto name = a["kind"].get<std::string>();
      RequestShaderJob(name == "cleanup" ? assistant::JobKind::cleanup
                       : name == "classification"
                           ? assistant::JobKind::classification
                           : assistant::JobKind::explanation,
                       id);
      return finish(
          {{"queued", m_ai_request.active || m_assistant.IsJobRunning()}});
    }
    throw std::runtime_error("Automation operation has no handler");
  } catch (const std::exception &e) {
    Json::Parse(
        J{{"ok", false}, {"error", cymcp::redact_error_message(e.what())}}
            .dump(),
        response);
    return true;
  }
}
} // namespace cygi
