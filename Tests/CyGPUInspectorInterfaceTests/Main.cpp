// Exercise the migrated native views and the application's editor adapter without a game.
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Editor/HlslEditor.hpp"
#include "App/TimingNavigation.hpp"
#include "Help/HelpLauncher.hpp"
#include <cyui/Core.h>
#include <cyui/modules/CodeEditor.h>
#include <cyui/modules/Diagnostics.h>
#include <cyui/modules/DocumentViews.h>
#include <cyui/modules/Images.h>
#include <cyui/modules/Inspector.h>
#include <cyui/modules/Timeline.h>
#include <TextEditor.h>
#include <imgui_internal.h>
#include <cstdio>
#include <cmath>
#include <functional>
#include <limits>
#include <fstream>
#include <chrono>

namespace
{
	int checks = 0, failures = 0;
	void Check(bool condition, const char *message)
	{
		++checks;
		if (!condition) { ++failures; std::printf("FAIL: %s\n", message); }
	}
	void Frame(const std::function<void()> &draw)
	{
		ImGui::NewFrame();
		ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({1000, 800});
		ImGui::Begin("Interface tests", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
		ImGui::SetCursorScreenPos({20, 20});
		draw();
		ImGui::Dummy({0, 0});
		ImGui::End(); ImGui::Render();
	}
	void Mouse(float x, float y, int button = -1, bool down = false)
	{
		auto &io = ImGui::GetIO(); io.AddMousePosEvent(x, y);
		if (button >= 0) io.AddMouseButtonEvent(button, down);
	}

	void Editor()
	{
		cygi::HlslEditor editor;
		const std::string source = "// caf\xc3\xa9\r\nfloat4 main() {\r\n\treturn 1;\r\n}";
		editor.SetText(source);
		Check(editor.GetText() == source && !editor.IsModified(), "loading preserves UTF-8, CRLF, tabs and absent final newline");
		editor.SetWordWrap(true); editor.SetShowWhitespace(false); editor.SetShowMiniMap(true);
		editor.Focus();
		auto draw = [&] { editor.Render("hlsl", {800, 450}); };
		Mouse(-100, -100); Frame(draw); Frame(draw);
		auto &io = ImGui::GetIO();
		editor.SelectAll();
		io.AddInputCharactersUTF8("x"); Frame(draw);
		Check(editor.GetText() != source && editor.IsModified() && editor.CanUndo(), "typing reaches the application's shared editor adapter");
		editor.Undo(); Frame(draw);
		Check(editor.GetText() == source && !editor.IsModified() && editor.CanRedo(), "Undo restores exact original bytes and dirty state");
		editor.Redo(); Frame(draw);
		Check(editor.GetText() == "x" && editor.IsModified(), "Redo restores the committed edit");
		io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_Z, true); Frame(draw);
		io.AddKeyEvent(ImGuiKey_Z, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame(draw);
		Check(editor.GetText() == source, "Ctrl+Z is handled by the focused common editor");
		io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_Y, true); Frame(draw);
		io.AddKeyEvent(ImGuiKey_Y, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame(draw);
		Check(editor.GetText() == "x", "Ctrl+Y restores the focused editor's transaction");
		const auto edited = editor.GetText();
		for (const auto &theme : cyui::builtInThemes())
			for (const auto density : {cyui::Density::compact, cyui::Density::comfortable})
			{
				cyui::Config config; config.themeKey = theme.key.c_str(); config.density = density; config.dpiScale = 1.25;
				cyui::apply(config); Frame(draw);
				Check(editor.GetText() == edited && editor.CanUndo(), "theme, density and DPI changes retain text and Undo");
			}
		const auto messages = editor.MarkCompilerMessages("shader.hlsl(2,3): error X3000: syntax error");
		Check(!messages.empty() && messages[0].line == 2, "compiler diagnostics retain source navigation");
		editor.ClearMarks(); editor.GoToLine(1); editor.OpenFind(); Frame(draw);
		editor.SetReadOnly(true); editor.Focus(); Frame(draw);
		io.AddInputCharactersUTF8("not allowed"); Frame(draw);
		Check(editor.GetText() == edited, "read-only reconstructed code cannot be edited");
		editor.SetReadOnly(false); editor.SetText(source);
		Check(!editor.CanUndo() && !editor.IsModified(), "loading another document starts a new host Undo history");
	}

	void TimelineAndImages()
	{
		cyui::apply();
		cyui::TimelineState state; state.view = {0, 10};
		const std::vector<cyui::TimelineLane> lanes = {{101, "Passes", 40}};
		const std::vector<cyui::TimelineClip> clips = {{7001, 101, 1, 3, "GBuffer", 0, true}};
		cyui::TimelineOptions options; options.readOnly = true; options.revision = 44;
		cyui::TimelineResult result;
		auto draw = [&] { result = cyui::timeline("gpu-timing", state, lanes, clips, {}, {700, 140}, options); };
		Mouse(300, 70); Frame(draw); Frame(draw);
		Mouse(300, 70, 0, true); Frame(draw);
		Mouse(360, 70); Frame(draw);
		Mouse(360, 70, 0, false); Frame(draw);
		Check(cyui::selected(state.selected, 7001), "GPU pass selection uses stable application IDs");
		Check(result.clips.phase == cyui::EditPhase::none && result.remove.empty() && clips[0].start == 1,
			"measured GPU spans remain immutable during a drag");
		const auto before = state.view;
		ImGui::GetIO().AddMouseWheelEvent(0, 1); Frame(draw);
		Check(state.view.valid() && state.view.span() < before.span(), "timeline wheel zoom");
		const auto zoomed = state.view;
		Mouse(350, 70, 2, true); Frame(draw); Mouse(390, 70); Frame(draw);
		Mouse(390, 70, 2, false); Frame(draw);
		Check(std::abs(state.view.min - zoomed.min) > .001, "timeline middle-drag pan");
		cygi::TimingNavigation navigation;
		auto compatible = [&] { draw(); navigation.Update(state); };
		const auto middle_panned = state.view;
		Mouse(350, 70, 0, true); Frame(compatible); Mouse(390, 70); Frame(compatible);
		Mouse(390, 70, 0, false); Frame(compatible);
		Check(navigation.dragged && state.view.min != middle_panned.min && result.clips.phase == cyui::EditPhase::none,
			"legacy left-drag pans a read-only timing view without emitting edits");
		const auto left_panned = state.view;
		Mouse(350, 70, 1, true); Frame(compatible); Mouse(390, 70); Frame(compatible);
		Mouse(390, 70, 1, false); Frame(compatible);
		Check(navigation.dragged && state.view.min != left_panned.min, "legacy right-drag pan remains available");
		cyui::ImageState image; cyui::ImageResult pixel;
		auto surface = [&] { pixel = cyui::imageViewport("image", image, {ImTextureID(42), 640, 360}, {700, 400}); };
		Mouse(350, 220); Frame(surface); Frame(surface);
		Check(!image.fitPending && pixel.pixelX >= 0 && pixel.pixelY >= 0, "image surface fits the host texture and reports pixel coordinates");
		const auto scale = image.canvas.view.zoom;
		ImGui::GetIO().AddMouseWheelEvent(0, 1); Frame(surface);
		Check(image.canvas.view.zoom > scale, "image wheel zoom");
	}

	void PropertiesAndDocuments()
	{
		cyui::setLocale("fr");
		Frame([] { Check(ImGui::GetID(cyui::trLabel("Save###document").c_str()) == ImGui::GetID("Save###document"),
			"French labels preserve stable widget identity"); });
		cyui::TextFilter filter; filter.query = "echantillons";
		cyui::registerTranslations("fr", {{"Samples", "\xc3\x89" "chantillons"}});
		const cyui::Property sample{11, "Samples", {}, int64_t(9007199254740993ll), true};
		Check(cyui::propertyMatches(filter, sample, "Pipeline state"), "pipeline quick filter matches translated accented labels");
		cyui::InspectorState state;
		std::vector<cyui::Property> properties{sample};
		Frame([&] { Check(cyui::inspector("pipeline", state, properties, 51).empty(), "captured pipeline properties do not emit edits"); });
		Check(std::get<int64_t>(properties[0].value) == 9007199254740993ll, "integer properties do not round through double");
		cyui::TranscriptState transcript;
		std::vector<cyui::TranscriptMessage> messages = {{91, "Assistant", "# Frame\n\n- GBuffer\n\n```hlsl\nfloat4 main() { return 1; }\n```\n", {}}};
		Frame([&] { cyui::transcript("chat", transcript, messages, {700, 500}); });
		messages.push_back({92, "You", "What is slow?", {}});
		Frame([&] { cyui::transcript("chat", transcript, messages, {700, 500}); });
		Check(transcript.documents.count(91) && transcript.documents.count(92), "transcript retains stable cached documents when appending");
		messages.erase(messages.begin());
		Frame([&] { cyui::transcript("chat", transcript, messages, {700, 500}); });
		Check(!transcript.documents.count(91) && transcript.documents.count(92), "conversation eviction drops only the removed message cache");
		cyui::LogBuffer log(2); cyui::LogState logs;
		log.append(cyui::LogLevel::info, "connected"); log.append(cyui::LogLevel::error, "REFUSED replace_shader");
		log.append(cyui::LogLevel::warning, "capture requested");
		Frame([&] { cyui::logView("mcp", logs, log, {700, 350}); });
		Check(logs.rows.size() == 2 && logs.rows[0].level == cyui::LogLevel::error, "bounded MCP log retains refusal severity");
		logs.query = "REFUSED";
		Frame([&] { cyui::logView("mcp", logs, log, {700, 350}); });
		Check(logs.filtered.size() == 1, "MCP log filtering");
	}
}

namespace
{
	void OfflineHelp()
	{
		const auto root = std::filesystem::temp_directory_path() /
			(std::wstring(L"CyGPUInspector help é # % ") + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(root / "DocData");
		auto write = [&](const char *name, const std::string &text) { std::ofstream file(root / name, std::ios::binary); file << text; };
		write("Documentation.html", "<!doctype html><title>Local help fixture</title>");
		write("DocData/manifest.json", R"({"schemaVersion":1,"aliases":{"ref-MCP":"ref-mcp","old-setup":"welcome#first-steps"},"pages":[
			{"id":"welcome","locale":"en","headings":[{"id":"first-steps"}],"anchors":[],"html":"<div data-cydoc-id=\"figure-0\"></div>"},
			{"id":"ref-mcp","locale":"en","headings":[],"html":"MCP"}]})");
		write("DocData/legacy-routes.json", R"({"welcome":{"fr":{"premiers-pas":"first-steps"}}})");
		std::string error;
		const auto index = root / "Documentation.html";
		auto url = cygi::HelpLauncher::BuildUrl(index,"welcome","en",error);
		Check(error.empty() && url.rfind(L"file:///",0) == 0, "CyDoc opens through a local file URL");
		Check(url.find(L"%20") != std::wstring::npos && url.find(L"%23") != std::wstring::npos && url.find(L"%25") != std::wstring::npos && url.find(L"%C3%A9") != std::wstring::npos,
			"Windows URL API encodes spaces, fragment characters and percent signs in installation paths");
		Check(url.find(L"#/en/welcome") != std::wstring::npos, "native route defaults to English");
		url = cygi::HelpLauncher::BuildUrl(index,"old-setup","en",error);
		Check(error.empty() && url.find(L"#/en/welcome/first-steps") != std::wstring::npos, "native aliases can retain a specific section");
		url = cygi::HelpLauncher::BuildUrl(index,"ref-MCP","fr",error);
		Check(error.empty() && url.find(L"#/fr/ref-mcp") != std::wstring::npos, "legacy page alias retains requested locale and English fallback");
		url = cygi::HelpLauncher::BuildUrl(index,"help:welcome#premiers-pas","fr",error);
		Check(error.empty() && url.find(L"/first-steps") != std::wstring::npos, "legacy translated anchors map to a common stable section");
		url = cygi::HelpLauncher::BuildUrl(index,"welcome?media=figure-0","en",error);
		Check(error.empty() && url.find(L"?media=figure-0") != std::wstring::npos, "media state is inside the route fragment");
		Check(cygi::HelpLauncher::BuildUrl(index,"unknown","en",error).empty() && !error.empty(), "unknown help page reports an error");
		Check(cygi::HelpLauncher::BuildUrl(index,"welcome#missing","en",error).empty(), "unknown section does not open a misleading destination");
		Check(cygi::HelpLauncher::BuildUrl(index,"welcome?media=missing","en",error).empty(), "unknown media route is rejected");
		Check(cygi::HelpLauncher::ContextTopic("Ressources###Resources") == "resources" &&
			cygi::HelpLauncher::ContextTopic("MCP") == "mcp", "contextual F1 uses stable panel identities across languages");
		Check(cygi::HelpLauncher::ContextTopic("HLSL editor") == "hlsl" &&
			cygi::HelpLauncher::ContextTopic("unrecognised") == "welcome", "contextual help has a predictable home fallback");
		write("DocData/manifest.json", "{}");
		Check(cygi::HelpLauncher::BuildUrl(index,"welcome","en",error).empty(), "missing or invalid CyDoc bundle is recoverable");
		for (const char *name : {"Documentation.html","DocData/manifest.json","DocData/legacy-routes.json"}) std::filesystem::remove(root / name);
		std::filesystem::remove(root / "DocData");
		std::filesystem::remove(root);
	}
}

int main()
{
	ImGui::CreateContext(); auto &io = ImGui::GetIO();
	io.IniFilename = nullptr; io.DisplaySize = {1000, 800}; io.DeltaTime = 1.f / 60;
	ImGui::GetCurrentContext()->ErrorCallback = [](ImGuiContext *, void *, const char *message) { Check(false, message); };
	io.ConfigErrorRecoveryEnableAssert = false;
	cyui::apply(); cyui::loadFonts();
	unsigned char *pixels; int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height); io.Fonts->SetTexID(ImTextureID(1));
	Editor(); TimelineAndImages(); PropertiesAndDocuments(); OfflineHelp();
	ImGui::DestroyContext();
	std::printf("%d interface checks, %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
