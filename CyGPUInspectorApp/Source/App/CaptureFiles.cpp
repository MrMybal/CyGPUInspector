// CyGPUInspectorApp — what a capture leaves on disk.
//
// Every deep capture gets a folder of its own, next to the executable:
//
//   Images/<game>_<date>_<time>_frame<N>/
//       final.png            what the game presented for the captured frame
//       01_rt_2560x1440_...  the textures that frame wrote to, as .png and .dds, when the
//                            capture was asked to save its buffers
//       capture.json         what was saved, and why some were not
//
// The point of a capture is to be able to look at that moment later, and the pixels are the part
// that is gone otherwise: all of this is written by itself, without a click.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Application.hpp"

#include <CyGPUInspectorCore/Format.hpp>
#include <CyGPUInspectorCore/Localization.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>

#include <d3d11.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace cygi
{
	using i18n::Tr;
	using i18n::TrId;

	namespace
	{
		std::filesystem::path ImagesDirectory()
		{
			wchar_t executable[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, executable, MAX_PATH);
			return std::filesystem::path(executable).parent_path() / "Images";
		}

		// A path as UTF-8: std::filesystem::path::string() converts through the ANSI code page, and
		// throws on a name it cannot represent.
		std::string Utf8(const std::filesystem::path &path)
		{
			const std::wstring wide = path.wstring();
			if (wide.empty())
				return std::string();
			const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr,
				0, nullptr, nullptr);
			if (length <= 0)
				return std::string();
			std::string text(static_cast<size_t>(length), ' ');
			WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), text.data(), length, nullptr,
				nullptr);
			return text;
		}

		std::string Timestamp()
		{
			SYSTEMTIME now = {};
			GetLocalTime(&now);
			char text[32];
			std::snprintf(text, sizeof(text), "%04u%02u%02u_%02u%02u%02u", now.wYear, now.wMonth, now.wDay,
				now.wHour, now.wMinute, now.wSecond);
			return text;
		}

		const char *StateLabel(CaptureBufferWriter::State state)
		{
			switch (state)
			{
			case CaptureBufferWriter::State::waiting: return Tr("waiting");
			case CaptureBufferWriter::State::writing: return Tr("writing");
			case CaptureBufferWriter::State::written: return Tr("saved");
			case CaptureBufferWriter::State::failed: return Tr("failed");
			case CaptureBufferWriter::State::not_copied: return Tr("not copied");
			default: return "";
			}
		}

		const char *RoleLabel(uint32_t roles)
		{
			if ((roles & kBufferRoleBackBuffer) != 0) return Tr("back buffer");
			if ((roles & kBufferRoleDepthStencil) != 0) return Tr("depth");
			if ((roles & kBufferRoleRenderTarget) != 0) return Tr("render target");
			if ((roles & kBufferRoleUnorderedAccess) != 0) return Tr("UAV");
			if ((roles & kBufferRoleCopyDest) != 0) return Tr("copy");
			return Tr("texture");
		}
	}

	Application::~Application()
	{
		ReleaseBufferView();
		// Joins the writer thread after it has written what it holds.
		m_buffer_writer.Shutdown();
	}

	std::string Application::GameName()
	{
		std::string process = "Game";
		if (m_client)
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			if (m_client->Model().HasSession())
				process = m_client->Model().Session().process_name;
		}
		const size_t dot = process.rfind('.');
		if (dot != std::string::npos)
			process.resize(dot);
		return process;
	}

	std::filesystem::path Application::CaptureFolder(uint64_t first_frame, uint64_t last_frame)
	{
		if (m_capture_folder_first == first_frame && !m_capture_folder.empty())
			return m_capture_folder;

		m_capture_folder = ImagesDirectory() / (GameName() + "_" + Timestamp() + "_frame" + std::to_string(last_frame));
		m_capture_folder_first = first_frame;
		std::error_code code;
		std::filesystem::create_directories(m_capture_folder, code);
		return m_capture_folder;
	}

	std::string Application::CaptureFolderText() const
	{
		return Utf8(m_capture_folder);
	}

	// What is on screen, to a PNG. The image of a capture goes into that capture's folder; any
	// other one next to the executable, named after the game and the frame, so a folder of them
	// sorts itself.
	void Application::SavePreviewImage()
	{
		if (!m_preview.IsValid())
		{
			m_preview_saved = Tr("Nothing to save yet.");
			return;
		}

		uint64_t frame = m_preview_frozen_sent ? m_preview_frozen_frame : 0;
		if (m_client && frame == 0)
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			frame = m_client->Model().LastFrame().index;
		}

		std::filesystem::path path;
		if (m_preview_capture_hold)
		{
			path = CaptureFolder(m_handled_capture_frame, frame) /
				(m_preview_resource == kPreviewFinalImage ? std::string("final.png")
				                                          : "preview_res" + std::to_string(m_preview_resource) + ".png");
		}
		else
		{
			const std::filesystem::path directory = ImagesDirectory();
			std::error_code code;
			std::filesystem::create_directories(directory, code);
			const std::string what = m_preview_resource == kPreviewFinalImage
				? std::string("final") : "resource" + std::to_string(m_preview_resource);
			path = directory / (GameName() + "_" + Timestamp() + "_frame" + std::to_string(frame) + "_" + what + ".png");
		}

		std::string error;
		if (m_preview_renderer_ready && m_preview_renderer.SavePng(path.wstring(), error))
			m_preview_saved = std::string(Tr("Saved: ")) + Utf8(path);
		else
			m_preview_saved = std::string(Tr("Could not save: ")) + (m_preview_renderer_ready ? error
				: std::string("the display pass is unavailable"));
	}

	void Application::UpdateCaptureBuffers()
	{
		if (!IsLive())
			return;

		std::vector<CaptureBufferRecord> records;
		std::vector<std::string> names;
		uint64_t game_frame = 0;
		{
			std::lock_guard<std::mutex> lock(m_client->Model().Mutex());
			m_client->Model().TakeCaptureBuffers(records);
			game_frame = m_client->Model().LastFrame().index;
			// The add-on sends the names before the buffers, so the files can carry them.
			for (const CaptureBufferRecord &record : records)
			{
				const ResourceInfo *resource = m_client->Model().ResourceById(record.resource_id);
				names.push_back(resource != nullptr ? resource->name : std::string());
			}
		}

		for (size_t i = 0; i < records.size(); ++i)
		{
			const CaptureBufferRecord &record = records[i];
			if (!m_buffer_writer.Active() || m_buffer_writer.FirstFrame() != record.first_frame)
			{
				ReleaseBufferView();
				m_buffer_writer.Begin(CaptureFolder(record.first_frame, record.frame_index), record.first_frame,
					GameName());
			}
			m_buffer_writer.Add(record, names[i]);
		}

		// Everything read back: the game gets its memory back now, not in a minute.
		if (m_buffer_writer.Step(game_frame))
			m_client->ReleaseCaptureBuffers();
	}

	void Application::ReleaseBufferView()
	{
		if (m_buffer_view != nullptr)
			m_buffer_view->Release();
		m_buffer_view = nullptr;
		m_buffer_view_index = -1;
		m_buffer_view_width = 0;
		m_buffer_view_height = 0;
		m_buffer_view_error.clear();
	}

	void Application::ShowBufferFile(int index, const std::filesystem::path &png)
	{
		ReleaseBufferView();
		m_buffer_view_index = index;
		m_buffer_view = LoadImageFile(m_device, png.wstring(), m_buffer_view_width, m_buffer_view_height,
			m_buffer_view_error);
	}

	void Application::OpenCaptureFolder()
	{
		if (!m_capture_folder.empty())
			ShellExecuteW(nullptr, L"open", m_capture_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	// The picture side of the Deep capture panel: the image being looked at on top — the final
	// image, or one saved buffer — and the list of buffers under it.
	void Application::DrawCaptureFiles(float width, float height)
	{
		const std::vector<CaptureBufferWriter::Item> items = m_buffer_writer.Items();
		const float row = ImGui::GetTextLineHeightWithSpacing();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		// Side by side in a wide panel — the list on the left, as long as it needs, and the picture
		// on the right — or the list under the picture in a tall one.
		const bool side_by_side = !items.empty() && width > height * 1.2f;
		const float list_width = side_by_side ? std::clamp(width * 0.5f, 340.0f, 640.0f) : width;
		const float list_height = items.empty() ? 0.0f : side_by_side ? height
			: std::min(height * 0.45f, row * (static_cast<float>(items.size()) + 2.5f));
		const float image_width = side_by_side ? width - list_width - spacing : width;
		const float image_height = side_by_side ? height
			: std::max(80.0f, height - list_height - ImGui::GetStyle().ItemSpacing.y);

		if (side_by_side)
		{
			DrawBufferList(items, list_width, list_height);
			ImGui::SameLine();
		}

		if (ImGui::BeginChild("capture-picture", ImVec2(image_width, image_height)))
		{
			const bool showing_buffer = m_buffer_view_index >= 0 &&
				m_buffer_view_index < static_cast<int>(items.size());
			if (!showing_buffer)
			{
				if (m_preview_capture_hold)
					DrawFrameImage(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
				else
					ImGui::TextDisabled(Tr("Select a buffer below."));
			}
			else
			{
				const CaptureBufferWriter::Item &item = items[static_cast<size_t>(m_buffer_view_index)];
				if (ImGui::SmallButton(TrId("Final image")))
					ReleaseBufferView();
				ImGui::SameLine();
				ImGui::TextUnformatted(item.file_stem.c_str());
				ImGui::TextDisabled(Tr("As it was at the end of frame %llu. PNG range %.4g to %.4g; the DDS holds the data."),
					static_cast<unsigned long long>(item.record.frame_index), static_cast<double>(item.range_min),
					static_cast<double>(item.range_max));
				if (m_buffer_view != nullptr && m_buffer_view_width != 0 && m_buffer_view_height != 0)
				{
					const ImVec2 avail = ImGui::GetContentRegionAvail();
					const float scale = std::min(avail.x / static_cast<float>(m_buffer_view_width),
						avail.y / static_cast<float>(m_buffer_view_height));
					ImGui::Image(reinterpret_cast<ImTextureID>(m_buffer_view),
						ImVec2(static_cast<float>(m_buffer_view_width) * scale,
						       static_cast<float>(m_buffer_view_height) * scale));
				}
				else if (!m_buffer_view_error.empty())
				{
					ImGui::TextDisabled(Tr("Could not show it: %s"), m_buffer_view_error.c_str());
				}
			}
		}
		ImGui::EndChild();

		if (!items.empty() && !side_by_side)
			DrawBufferList(items, list_width, list_height);
	}

	void Application::DrawBufferList(const std::vector<CaptureBufferWriter::Item> &items, float width, float height)
	{
		if (ImGui::BeginChild("capture-buffers", ImVec2(width, height), ImGuiChildFlags_Borders))
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV;
			if (ImGui::BeginTable("buffers", 5, flags))
			{
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f);
				ImGui::TableSetupColumn(Tr("Role"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
				ImGui::TableSetupColumn(Tr("Size"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableSetupColumn(Tr("Format"), ImGuiTableColumnFlags_WidthStretch, 1.7f);
				ImGui::TableSetupColumn(Tr("File"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableHeadersRow();

				for (size_t i = 0; i < items.size(); ++i)
				{
					const CaptureBufferWriter::Item &item = items[i];
					const CaptureBufferRecord &record = item.record;
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					char label[32];
					std::snprintf(label, sizeof(label), "%02u##buffer%zu", record.index + 1, i);
					const bool saved = item.state == CaptureBufferWriter::State::written;
					if (ImGui::Selectable(label, m_buffer_view_index == static_cast<int>(i),
					                      ImGuiSelectableFlags_SpanAllColumns) && saved)
						ShowBufferFile(static_cast<int>(i), m_buffer_writer.Folder() / (item.file_stem + ".png"));
					if (ImGui::IsItemHovered() && !item.error.empty())
						ImGui::SetTooltip("%s", item.error.c_str());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(RoleLabel(record.roles));
					ImGui::TableNextColumn();
					ImGui::Text("%u x %u", record.width, record.height);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(FormatName(record.source_format));
					ImGui::TableNextColumn();
					if (saved)
						ImGui::TextUnformatted(StateLabel(item.state));
					else
						ImGui::TextDisabled("%s", StateLabel(item.state));
				}
				ImGui::EndTable();
			}
		}
		ImGui::EndChild();
	}
}
