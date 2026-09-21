// CyGPUInspectorApp — the buffers of a deep capture, from the game's GPU to files.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CaptureBufferWriter.hpp"

#include "Render/SharedTexture.hpp"

#include <CyGPUInspectorCore/Format.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3d11.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace cygi
{
	namespace
	{
		// The copies are recorded at the end of a frame and run after it. Games keep up to three
		// frames in flight: past that many presents, the GPU has certainly made them.
		constexpr uint64_t kFramesInFlight = 3;
		// Records the ring could not carry never arrive: stop waiting for them after a while.
		constexpr uint64_t kGiveUpFrames = 600;
		// Read backs waiting for the worker: each is a full image in memory.
		constexpr size_t kMaxQueuedJobs = 3;

		float HalfToFloat(uint16_t half)
		{
			const uint32_t sign = (half >> 15) & 1u;
			const uint32_t exponent = (half >> 10) & 0x1Fu;
			const uint32_t mantissa = half & 0x3FFu;
			float value = 0.0f;
			if (exponent == 0)
				value = std::ldexp(static_cast<float>(mantissa), -24);
			else if (exponent == 31)
				value = mantissa == 0 ? INFINITY : NAN;
			else
				value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
			return sign != 0 ? -value : value;
		}

		// The unsigned 11 and 10 bit floats of r11g11b10: five bits of exponent, no sign.
		float SmallFloat(uint32_t bits, uint32_t mantissa_bits)
		{
			const uint32_t exponent = (bits >> mantissa_bits) & 0x1Fu;
			const uint32_t mantissa = bits & ((1u << mantissa_bits) - 1u);
			if (exponent == 0)
				return std::ldexp(static_cast<float>(mantissa), -14 - static_cast<int>(mantissa_bits));
			if (exponent == 31)
				return mantissa == 0 ? INFINITY : NAN;
			return std::ldexp(static_cast<float>(mantissa | (1u << mantissa_bits)),
				static_cast<int>(exponent) - 15 - static_cast<int>(mantissa_bits));
		}

		// The colour (or depth) values of one texel, for the formats whose range is worth
		// measuring: floats, which can go past 1 or below 0, and depth. Returns how many values
		// it wrote, 0 for a format read as 0 to 1 anyway. Alpha is left out: it is not drawn.
		uint32_t DecodeTexel(uint32_t view_format, const uint8_t *texel, float out[3])
		{
			switch (view_format)
			{
			case 2:    // r32g32b32a32_float
				std::memcpy(out, texel, 12);
				return 3;
			case 10:   // r16g16b16a16_float
			case 34:   // r16g16_float
			case 54:   // r16_float
			{
				const uint32_t channels = view_format == 10 ? 3 : (view_format == 34 ? 2 : 1);
				for (uint32_t c = 0; c < channels; ++c)
				{
					uint16_t half = 0;
					std::memcpy(&half, texel + c * 2, 2);
					out[c] = HalfToFloat(half);
				}
				return channels;
			}
			case 16:   // r32g32_float
				std::memcpy(out, texel, 8);
				return 2;
			case 41:   // r32_float
			case 21:   // r32_float_x8x24_typeless: the depth of d32_float_s8
				std::memcpy(out, texel, 4);
				return 1;
			case 26:   // r11g11b10_float
			{
				uint32_t packed = 0;
				std::memcpy(&packed, texel, 4);
				out[0] = SmallFloat(packed & 0x7FFu, 6);
				out[1] = SmallFloat((packed >> 11) & 0x7FFu, 6);
				out[2] = SmallFloat((packed >> 22) & 0x3FFu, 5);
				return 3;
			}
			case 46:   // r24_unorm_x8_typeless: the depth of d24_unorm_s8
			{
				uint32_t packed = 0;
				std::memcpy(&packed, texel, 4);
				out[0] = static_cast<float>(packed & 0xFFFFFFu) / 16777215.0f;
				return 1;
			}
			case 56:   // r16_unorm: the depth of d16
			{
				uint16_t value = 0;
				std::memcpy(&value, texel, 2);
				out[0] = static_cast<float>(value) / 65535.0f;
				return 1;
			}
			default:
				return 0;
			}
		}

		std::string Sanitize(const std::string &text)
		{
			std::string out;
			for (const char c : text)
			{
				const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
					c == '_' || c == '-' || c == '.';
				out.push_back(keep ? c : '_');
				if (out.size() >= 96)
					break;
			}
			return out;
		}

		const char *RoleWord(uint32_t roles)
		{
			if ((roles & kBufferRoleBackBuffer) != 0) return "backbuffer";
			if ((roles & kBufferRoleDepthStencil) != 0) return "depth";
			if ((roles & kBufferRoleRenderTarget) != 0) return "rt";
			if ((roles & kBufferRoleUnorderedAccess) != 0) return "uav";
			if ((roles & kBufferRoleCopyDest) != 0) return "copy";
			return "texture";
		}

		std::string JsonEscape(const std::string &text)
		{
			std::string out;
			for (const char c : text)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char escaped[8];
						std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
						out += escaped;
					}
					else
					{
						out.push_back(c);
					}
				}
			}
			return out;
		}

		void CloseUnreadHandle(const CaptureBufferRecord &record)
		{
			// An NT handle duplicated into this process is ours to close, read or not.
			if (record.is_nt_handle != 0 && record.shared_handle != 0)
				CloseHandle(reinterpret_cast<HANDLE>(record.shared_handle));
		}

		// DDS with the DX10 extension header: one 2D texture, one mip, one layer.
		void AppendDdsHeader(std::vector<uint8_t> &out, uint32_t dxgi_format, uint32_t width, uint32_t height,
		                     uint32_t row_pitch)
		{
			uint32_t header[1 + 31 + 5] = {};
			header[0] = 0x20534444u;                 // "DDS "
			uint32_t *dds = header + 1;
			dds[0] = 124;                            // dwSize
			dds[1] = 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u | 0x20000u;   // caps, height, width, pitch, format, mips
			dds[2] = height;
			dds[3] = width;
			dds[4] = row_pitch;
			dds[5] = 0;                              // depth
			dds[6] = 1;                              // mip count
			uint32_t *pixel_format = dds + 18;       // after dwReserved1[11]
			pixel_format[0] = 32;
			pixel_format[1] = 0x4u;                  // DDPF_FOURCC
			pixel_format[2] = 0x30315844u;           // "DX10"
			dds[26] = 0x1000u;                       // DDSCAPS_TEXTURE
			uint32_t *dx10 = header + 1 + 31;
			dx10[0] = dxgi_format;
			dx10[1] = 3;                             // D3D10_RESOURCE_DIMENSION_TEXTURE2D
			dx10[2] = 0;
			dx10[3] = 1;                             // array size
			dx10[4] = 0;
			const uint8_t *bytes = reinterpret_cast<const uint8_t *>(header);
			out.insert(out.end(), bytes, bytes + sizeof(header));
		}
	}

	CaptureBufferWriter::~CaptureBufferWriter()
	{
		Shutdown();
	}

	bool CaptureBufferWriter::Initialize(ID3D11Device *device, ID3D11DeviceContext *context)
	{
		m_device = device;
		m_context = context;
		m_renderer_ready = m_renderer.Initialize(device, context);
		m_stop = false;
		if (!m_worker.joinable())
			m_worker = std::thread([this] { WorkerLoop(); });
		return m_renderer_ready;
	}

	void CaptureBufferWriter::Shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stop = true;
		}
		m_wake.notify_all();
		if (m_worker.joinable())
			m_worker.join();

		std::lock_guard<std::mutex> lock(m_mutex);
		for (const Item &item : m_items)
			if (item.state == State::waiting)
				CloseUnreadHandle(item.record);
		m_items.clear();
		m_jobs.clear();
		m_active = false;
		m_renderer.Shutdown();
		m_renderer_ready = false;
	}

	void CaptureBufferWriter::Begin(const std::filesystem::path &folder, uint64_t first_frame, const std::string &game)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const Item &item : m_items)
			if (item.state == State::waiting)
				CloseUnreadHandle(item.record);
		m_items.clear();
		// Files of the previous capture still queued are written all the same; only their
		// progress no longer has anywhere to go.
		++m_generation;
		m_folder = folder;
		m_first_frame = first_frame;
		m_game = game;
		m_copied_at_frame = 0;
		m_expected = 0;
		m_release_sent = false;
		m_index_written = false;
		m_active = true;

		std::error_code code;
		std::filesystem::create_directories(m_folder, code);
	}

	void CaptureBufferWriter::Add(const CaptureBufferRecord &record, const std::string &name)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_active || record.first_frame != m_first_frame)
		{
			CloseUnreadHandle(record);
			return;
		}

		Item item;
		item.record = record;
		item.name = name;
		item.depth = (record.roles & kBufferRoleDepthStencil) != 0 || FormatIsDepth(record.source_format);
		// "03_depth_2560x1440_d32_float_res812": sorted like the frame, readable in a file list.
		char stem[160];
		std::snprintf(stem, sizeof(stem), "%02u_%s_%ux%u_%s_%s", record.index + 1, RoleWord(record.roles),
			record.width, record.height, FormatName(record.source_format),
			name.empty() ? ("res" + std::to_string(record.resource_id)).c_str() : Sanitize(name).c_str());
		item.file_stem = Sanitize(stem);
		if (record.status != PreviewStatus::ready)
		{
			item.state = State::not_copied;
			item.error = PreviewStatusName(record.status);
		}
		m_items.push_back(std::move(item));
		m_expected = record.count;
		m_copied_at_frame = std::max(m_copied_at_frame, record.frame_index);
	}

	bool CaptureBufferWriter::ReadBack(Item &item, Job &job, std::string &error)
	{
		const CaptureBufferRecord &record = item.record;
		SharedTexture texture;
		if (!texture.Open(m_device, record.shared_handle, record.is_nt_handle != 0, record.format, record.width,
		                  record.height))
		{
			error = texture.LastError();
			return false;
		}
		const uint32_t width = texture.Width();
		const uint32_t height = texture.Height();
		const uint32_t view_format = FormatShaderResourceView(record.format);

		// The data, as it is: what the DDS file holds, and what the PNG range is measured on.
		D3D11_TEXTURE2D_DESC desc = {};
		texture.Texture()->GetDesc(&desc);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		ID3D11Texture2D *staging = nullptr;
		if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &staging)))
		{
			error = "could not create a staging texture";
			return false;
		}
		m_context->CopyResource(staging, texture.Texture());
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(m_context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
		{
			staging->Release();
			error = "could not read the buffer back";
			return false;
		}

		const uint32_t bytes_per_pixel = FormatBytesPerPixel(record.format);
		if (bytes_per_pixel != 0)
		{
			const uint32_t row_bytes = width * bytes_per_pixel;
			job.dds.reserve(148 + static_cast<size_t>(row_bytes) * height);
			AppendDdsHeader(job.dds, view_format, width, height, row_bytes);
			for (uint32_t y = 0; y < height; ++y)
			{
				const uint8_t *row = static_cast<const uint8_t *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
				job.dds.insert(job.dds.end(), row, row + row_bytes);
			}
		}

		// The range the PNG is drawn with. Depth: between its nearest and farthest values, the
		// cleared ones (exactly 0 or 1) left out, or a raw depth buffer is one flat grey. Floats:
		// 0 to 1, widened to what the image really holds when it goes past — HDR colour, signed
		// vectors — with the extreme half percent on either side ignored so one hot pixel does
		// not flatten the rest. Everything else reads 0 to 1 already.
		item.range_min = 0.0f;
		item.range_max = 1.0f;
		if (bytes_per_pixel != 0)
		{
			const uint32_t step = std::max(1u, static_cast<uint32_t>(std::sqrt(
				static_cast<double>(width) * height / 262144.0)));
			std::vector<float> lows;
			std::vector<float> highs;
			float depth_min = INFINITY;
			float depth_max = -INFINITY;
			for (uint32_t y = 0; y < height; y += step)
			{
				const uint8_t *row = static_cast<const uint8_t *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
				for (uint32_t x = 0; x < width; x += step)
				{
					float values[3] = {};
					const uint32_t count = DecodeTexel(view_format, row + static_cast<size_t>(x) * bytes_per_pixel, values);
					if (count == 0)
						break;
					float low = INFINITY;
					float high = -INFINITY;
					for (uint32_t c = 0; c < count; ++c)
						if (std::isfinite(values[c]))
						{
							low = std::min(low, values[c]);
							high = std::max(high, values[c]);
						}
					if (!std::isfinite(low))
						continue;
					if (item.depth)
					{
						if (low > 0.0f && low < 1.0f)
						{
							depth_min = std::min(depth_min, low);
							depth_max = std::max(depth_max, low);
						}
					}
					else
					{
						lows.push_back(low);
						highs.push_back(high);
					}
				}
			}
			if (item.depth && depth_min < depth_max)
			{
				item.range_min = depth_min;
				item.range_max = depth_max;
			}
			else if (!item.depth && !lows.empty())
			{
				const size_t cut = lows.size() / 200;
				std::nth_element(lows.begin(), lows.begin() + cut, lows.end());
				std::nth_element(highs.begin(), highs.end() - 1 - cut, highs.end());
				item.range_min = std::min(0.0f, lows[cut]);
				item.range_max = std::max(1.0f, highs[highs.size() - 1 - cut]);
			}
		}
		m_context->Unmap(staging, 0);
		staging->Release();

		// Something to look at: the display pass the preview uses, with that range.
		if (m_renderer_ready)
		{
			PreviewSettings settings;
			settings.mode = item.depth ? PreviewChannelMode::depth_raw : PreviewChannelMode::color;
			settings.range_min = item.range_min;
			settings.range_max = item.range_max;
			settings.checkerboard_alpha = false;
			if (m_renderer.Render(texture.View(), width, height, settings) == nullptr ||
			    !m_renderer.ReadPixels(job.bgra, job.width, job.height, error))
			{
				job.bgra.clear();
				if (job.dds.empty())
				{
					if (error.empty())
						error = "the display pass failed";
					return false;
				}
			}
		}

		job.dds_path = m_folder / (item.file_stem + ".dds");
		job.png_path = m_folder / (item.file_stem + ".png");
		return true;
		// `texture` closes here, and with it the handle: the add-on's copy is not needed any more.
	}

	bool CaptureBufferWriter::Step(uint64_t game_frame)
	{
		if (!m_active)
			return false;

		size_t next = SIZE_MAX;
		bool all_arrived = false;
		bool all_written = true;
		size_t queued = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			for (size_t i = 0; i < m_items.size(); ++i)
			{
				if (m_items[i].state == State::waiting && next == SIZE_MAX)
					next = i;
				if (m_items[i].state == State::waiting || m_items[i].state == State::writing)
					all_written = false;
			}
			all_arrived = m_expected != 0 && m_items.size() >= m_expected;
			queued = m_jobs.size();
		}

		if (next != SIZE_MAX)
		{
			if (game_frame < m_copied_at_frame + kFramesInFlight || queued >= kMaxQueuedJobs)
				return false;

			Item item;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				item = m_items[next];
			}
			Job job;
			std::string error;
			const bool ok = ReadBack(item, job, error);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				// ReadBack opened (and closed) the handle whatever happened: never close it twice.
				item.record.shared_handle = 0;
				item.state = ok ? State::writing : State::failed;
				item.error = ok ? std::string() : error;
				m_items[next] = item;
				if (ok)
				{
					job.index = next;
					job.generation = m_generation;
					m_jobs.push_back(std::move(job));
				}
			}
			if (ok)
				m_wake.notify_one();
			return false;
		}

		if (!m_release_sent && (all_arrived || game_frame > m_copied_at_frame + kGiveUpFrames))
		{
			m_release_sent = true;
			return true;
		}

		if (m_release_sent && all_written && !m_index_written)
		{
			m_index_written = true;
			WriteIndex();
		}
		return false;
	}

	void CaptureBufferWriter::WorkerLoop()
	{
		for (;;)
		{
			Job job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
				// Stopping still writes what was read back: those pixels exist nowhere else.
				if (m_jobs.empty())
					return;
				job = std::move(m_jobs.front());
				m_jobs.pop_front();
			}

			std::string error;
			bool ok = true;
			if (!job.dds.empty())
			{
				std::ofstream file(job.dds_path, std::ios::binary | std::ios::trunc);
				file.write(reinterpret_cast<const char *>(job.dds.data()), static_cast<std::streamsize>(job.dds.size()));
				if (!file)
				{
					ok = false;
					error = "could not write " + job.dds_path.filename().string();
				}
			}
			if (!job.bgra.empty())
			{
				std::string png_error;
				if (!WritePng(job.png_path.wstring(), job.bgra, job.width, job.height, png_error))
				{
					ok = false;
					error = png_error;
				}
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			if (job.generation == m_generation && job.index < m_items.size())
			{
				m_items[job.index].state = ok ? State::written : State::failed;
				m_items[job.index].error = error;
			}
		}
	}

	std::vector<CaptureBufferWriter::Item> CaptureBufferWriter::Items() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_items;
	}

	void CaptureBufferWriter::Progress(uint32_t &done, uint32_t &total) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		done = 0;
		total = static_cast<uint32_t>(std::max<size_t>(m_items.size(), m_expected));
		for (const Item &item : m_items)
			if (item.state != State::waiting && item.state != State::writing)
				++done;
	}

	void CaptureBufferWriter::WriteIndex()
	{
		const std::vector<Item> items = Items();

		std::string json = "{\n";
		json += "  \"game\": \"" + JsonEscape(m_game) + "\",\n";
		json += "  \"first_frame\": " + std::to_string(m_first_frame) + ",\n";
		json += "  \"frame\": " + std::to_string(m_copied_at_frame) + ",\n";
		json += "  \"note\": \"Each buffer is copied as it was at the end of the frame: a texture several passes "
		        "write to shows the last of them. PNG files are a view (range below); DDS files hold the data.\",\n";
		json += "  \"buffers\": [\n";
		for (size_t i = 0; i < items.size(); ++i)
		{
			const Item &item = items[i];
			const CaptureBufferRecord &record = item.record;
			const bool written = item.state == State::written;
			char line[768];
			std::snprintf(line, sizeof(line),
				"    { \"index\": %u, \"resource_id\": %u, \"name\": \"%s\", \"roles\": \"%s\", \"width\": %u, "
				"\"height\": %u, \"format\": \"%s\", \"first_event\": %u, \"saved\": %s, \"status\": \"%s\", "
				"\"png\": \"%s\", \"dds\": \"%s\", \"range\": [%g, %g] }%s\n",
				record.index + 1, record.resource_id, JsonEscape(item.name).c_str(), RoleWord(record.roles),
				record.width, record.height, FormatName(record.source_format), record.first_event,
				written ? "true" : "false", JsonEscape(written ? "ok" : item.error).c_str(),
				written ? (item.file_stem + ".png").c_str() : "", written ? (item.file_stem + ".dds").c_str() : "",
				static_cast<double>(item.range_min), static_cast<double>(item.range_max),
				i + 1 < items.size() ? "," : "");
			json += line;
		}
		json += "  ]\n}\n";

		std::ofstream file(m_folder / "capture.json", std::ios::binary | std::ios::trunc);
		file.write(json.data(), static_cast<std::streamsize>(json.size()));
	}
}
