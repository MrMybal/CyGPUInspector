// CyGPUInjector — the ReShade overlay tab.
//
// Small on purpose. Someone running a mod wants to know three things: what loaded, whether it is
// doing anything, and how to turn it off. Everything else belongs in CyGPUInspector.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Overlay.hpp"

#include <CyGPUInspectorCore/Localization.hpp>

#include "ModLibrary.hpp"

#include <imgui.h>
#include <reshade.hpp>

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
		void DrawOverlay(reshade::api::effect_runtime *)
		{
			ModLibrary &library = Library();

			ImGui::TextUnformatted(Tr("Modifications made with CyGPUInspector, applied here."));
			ImGui::TextDisabled(Tr("Folder: %s"), library.Root().c_str());
			ImGui::Separator();

			if (library.Packages().empty())
			{
				ImGui::TextWrapped(Tr("No mod package found. Put a *.cygimod folder in the folder above "
				                      "and restart the game: shader replacements are applied when the "
				                      "game creates its pipelines, which it does at load time."));
				return;
			}

			ImGui::Text(Tr("%u shader(s) replaced, %u draw(s) skipped since start"),
				library.ReplacementsApplied(), library.DrawsSkipped());
			ImGui::SameLine();
			if (ImGui::SmallButton(TrId("Reset counters")))
				library.ResetCounters();

			ImGui::Separator();

			bool rebuild = false;
			for (LoadedPackage &package : library.Packages())
			{
				ImGui::PushID(package.directory.c_str());

				if (!package.loaded)
				{
					ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "%s", package.directory.c_str());
					ImGui::Indent();
					ImGui::TextWrapped("%s", package.error.c_str());
					ImGui::Unindent();
					ImGui::PopID();
					continue;
				}

				if (ImGui::Checkbox("##enabled", &package.enabled))
					rebuild = true;
				ImGui::SameLine();

				const ModPackage &mod = package.package;
				if (ImGui::TreeNode(mod.name.empty() ? package.directory.c_str() : mod.name.c_str()))
				{
					if (!mod.author.empty())
						ImGui::TextDisabled(Tr("by %s%s"), mod.author.c_str(),
							mod.version.empty() ? "" : (", version " + mod.version).c_str());
					if (!mod.description.empty())
						ImGui::TextWrapped("%s", mod.description.c_str());

					ImGui::Text(Tr("%u replacement(s), %u disable(s), %u matched in this game"),
						mod.CountOf(ModAction::replace), mod.CountOf(ModAction::disable),
						package.matched_shaders);

					// The package says which game it was made on, and it is worth showing when it
					// is not this one: it explains a package that loads but never matches.
					if (!mod.target_process.empty())
						ImGui::TextDisabled(Tr("made on %s (%s)"), mod.target_process.c_str(),
							mod.target_api.c_str());

					if (ImGui::TreeNode(TrId("Entries")))
					{
						for (const ModEntry &entry : mod.entries)
						{
							ImGui::BulletText("%s", entry.Describe().c_str());
							if (!entry.note.empty())
							{
								ImGui::Indent();
								ImGui::TextDisabled("%s", entry.note.c_str());
								ImGui::Unindent();
							}
						}
						ImGui::TreePop();
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			if (rebuild)
			{
				library.Rebuild();
				ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.45f, 1.0f),
					Tr("Draw suppressions change immediately. A shader replacement only changes when "
					   "the game next creates that pipeline, which usually means reloading the level."));
			}
		}
	}

	void RegisterOverlay()
	{
		reshade::register_overlay("CyGPUInjector", DrawOverlay);
	}

	void UnregisterOverlay()
	{
		reshade::unregister_overlay("CyGPUInjector", DrawOverlay);
	}
}
