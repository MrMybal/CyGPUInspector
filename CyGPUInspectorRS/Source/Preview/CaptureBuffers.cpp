// CyGPUInspectorRS — the buffers of a deep capture.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CaptureBuffers.hpp"

#include "Addon/Log.hpp"
#include "Preview/PreviewBridge.hpp"

#include <CyGPUInspectorCore/Format.hpp>

#include <algorithm>

using namespace reshade::api;

namespace cygi
{
	void CaptureBuffers::BeginCapture(uint64_t frame_index)
	{
		Release(frame_index);
		m_candidates.clear();
		m_states.clear();
	}

	void CaptureBuffers::NoteStates(const FrameRecorder::CapturedState &captured)
	{
		// The transitions of every captured frame, in the order the queue runs them: a texture the
		// last frame never transitioned is still in the state an earlier frame left it in.
		for (const CommandListState::RecordedBarrierSet &set : captured.barrier_sets)
			for (uint32_t i = 0; i < set.record.count; ++i)
			{
				const size_t at = static_cast<size_t>(set.entry_offset) + i;
				if (at >= captured.barrier_entries.size())
					break;
				const BarrierEntry &entry = captured.barrier_entries[at];
				if (entry.resource_id != 0 && entry.new_state != 0)
					m_states[entry.resource_id] = entry.new_state;
			}
	}

	void CaptureBuffers::NoteFrame(const std::vector<FrameEvent> &events, const FrameRecorder::CapturedState &captured,
	                               uint32_t first_event, uint32_t last_event)
	{
		// The textures this frame wrote to, each once, with everything it was to the frame.
		m_candidates.clear();
		std::unordered_map<uint32_t, size_t> position;
		auto add = [&](uint32_t resource_id, uint32_t role, uint32_t event_index) {
			if (resource_id == 0)
				return;
			const auto found = position.find(resource_id);
			if (found != position.end())
			{
				Candidate &candidate = m_candidates[found->second];
				candidate.roles |= role;
				candidate.first_event = std::min(candidate.first_event, event_index);
				return;
			}
			position.emplace(resource_id, m_candidates.size());
			m_candidates.push_back({ resource_id, role, event_index });
		};

		for (const FrameEvent &event : events)
		{
			if (event.index < first_event || event.index > last_event)
				continue;
			switch (event.kind)
			{
			case EventKind::bind_render_targets:
			case EventKind::begin_render_pass:
				// Draws only carry target 0: the binding is where targets 1 to 3 of a GBuffer show.
				add(event.primary_resource, kBufferRoleRenderTarget, event.index);
				if (event.a > 1)
					add(event.b, kBufferRoleRenderTarget, event.index);
				if (event.a > 2)
					add(event.c, kBufferRoleRenderTarget, event.index);
				if (event.a > 3)
					add(event.d, kBufferRoleRenderTarget, event.index);
				add(event.secondary_resource, kBufferRoleDepthStencil, event.index);
				break;
			case EventKind::clear_render_target:
				add(event.primary_resource, kBufferRoleRenderTarget, event.index);
				break;
			case EventKind::clear_depth_stencil:
				add(event.primary_resource, kBufferRoleDepthStencil, event.index);
				break;
			case EventKind::clear_unordered_access:
				add(event.primary_resource, kBufferRoleUnorderedAccess, event.index);
				break;
			case EventKind::copy_resource:
			case EventKind::copy_texture_region:
			case EventKind::copy_buffer_to_texture:
			case EventKind::resolve:
				add(event.primary_resource, kBufferRoleCopyDest, event.index);
				break;
			default:
				if (IsDrawOrDispatch(event.kind))
				{
					add(event.primary_resource, kBufferRoleRenderTarget, event.index);
					add(event.secondary_resource, kBufferRoleDepthStencil, event.index);
				}
				break;
			}
		}

		// With bindings recorded: targets 4 to 7, and what compute shaders write to.
		for (const CommandListState::RecordedDrawState &draw : captured.draw_states)
		{
			if (draw.record.event_index < first_event || draw.record.event_index > last_event)
				continue;
			for (uint32_t i = 0; i < draw.record.binding_count; ++i)
			{
				const size_t at = static_cast<size_t>(draw.binding_offset) + i;
				if (at >= captured.bindings.size())
					break;
				const DrawBinding &binding = captured.bindings[at];
				switch (static_cast<SlotKind>(binding.kind))
				{
				case SlotKind::render_target:
					add(binding.resource_id, kBufferRoleRenderTarget, draw.record.event_index);
					break;
				case SlotKind::depth_stencil:
					add(binding.resource_id, kBufferRoleDepthStencil, draw.record.event_index);
					break;
				case SlotKind::unordered_access:
					add(binding.resource_id, kBufferRoleUnorderedAccess, draw.record.event_index);
					break;
				default:
					break;
				}
			}
		}

		// In the order the frame first used them, which is the order a person reads a frame in.
		std::stable_sort(m_candidates.begin(), m_candidates.end(),
			[](const Candidate &a, const Candidate &b) { return a.first_event < b.first_event; });
	}

	std::vector<CaptureBufferRecord> CaptureBuffers::Snapshot(device *device, command_queue *queue,
	                                                          const ResourceTracker &resources, uint64_t first_frame,
	                                                          uint64_t frame_index, uint32_t app_process_id)
	{
		std::vector<CaptureBufferRecord> out;
		Release(frame_index);
		if (device == nullptr || queue == nullptr)
			return out;

		const device_api api = device->get_api();
		// D3D11 and OpenGL track states themselves; D3D12 and Vulkan need to be told the truth.
		const bool explicit_states = api == device_api::d3d12 || api == device_api::vulkan;
		const bool nt_handle = device->check_capability(device_caps::shared_resource_nt_handle);
		const bool can_share = nt_handle || device->check_capability(device_caps::shared_resource);
		command_list *cmd = queue->get_immediate_command_list();

		for (const Candidate &candidate : m_candidates)
		{
			ResourceRecord info = {};
			const resource source = resources.HandleOf(candidate.resource_id);
			if (source.handle == 0 || !resources.CopyRecord(candidate.resource_id, info))
				continue;   // destroyed during the frame: nothing left to copy
			if (info.kind != ResourceKind::texture_2d && info.kind != ResourceKind::surface)
				continue;   // buffers written by compute are not images

			const resource_desc desc = device->get_resource_desc(source);
			const uint32_t source_format = static_cast<uint32_t>(desc.texture.format);
			const uint32_t shared_format = FormatShareableCopyTarget(source_format);

			CaptureBufferRecord record = {};
			record.first_frame = first_frame;
			record.frame_index = frame_index;
			record.resource_id = candidate.resource_id;
			record.roles = candidate.roles | ((info.usage_flags & kUsageBackBuffer) != 0 ? kBufferRoleBackBuffer : 0u);
			record.first_event = candidate.first_event;
			record.width = desc.texture.width;
			record.height = desc.texture.height;
			record.format = shared_format;
			record.source_format = source_format;
			record.status = PreviewStatus::ready;

			// The state the copy takes the texture out of and puts it back into: `present` for a
			// swap chain buffer, else the one its last recorded transition left it in. A texture no
			// transition was seen for is not copied on D3D12 or Vulkan: guessing is undefined
			// behaviour there, and a wrong guess on a compressed depth buffer shows.
			resource_usage state = resource_usage::shader_resource;
			bool state_known = !explicit_states;
			if ((record.roles & kBufferRoleBackBuffer) != 0)
			{
				state = resource_usage::present;
				state_known = true;
			}
			else if (const auto found = m_states.find(candidate.resource_id); found != m_states.end())
			{
				state = static_cast<resource_usage>(found->second);
				state_known = true;
			}

			const uint32_t bytes_per_pixel = FormatBytesPerPixel(shared_format);
			const uint64_t bytes = static_cast<uint64_t>(record.width) * record.height *
				(bytes_per_pixel != 0 ? bytes_per_pixel : 4);

			if (desc.type != resource_type::texture_2d && desc.type != resource_type::surface)
				record.status = PreviewStatus::not_a_texture;
			else if (!FormatIsPreviewSupported(source_format))
				record.status = PreviewStatus::unsupported_format;
			else if (desc.texture.samples > 1)
				record.status = PreviewStatus::multisampled;
			else if (!can_share)
				record.status = PreviewStatus::sharing_unsupported;
			else if (cmd == nullptr)
				record.status = PreviewStatus::creation_failed;
			else if (!state_known)
				record.status = PreviewStatus::state_unknown;
			else if (m_held.size() >= kMaxBuffers || m_held_bytes + bytes > kMaxBytes)
				record.status = PreviewStatus::over_budget;

			if (record.status == PreviewStatus::ready)
			{
				resource texture = {};
				uint64_t local_handle = 0;
				record.status = CreateSharedCopyTarget(device, record.width, record.height, shared_format, nt_handle,
					texture, local_handle);
				if (record.status == PreviewStatus::ready)
				{
					record.shared_handle = ShareHandleWith(local_handle, app_process_id, nt_handle);
					record.is_nt_handle = nt_handle ? 1u : 0u;
					if (record.shared_handle == 0)
					{
						device->destroy_resource(texture);
						record.status = PreviewStatus::handle_duplication_failed;
					}
					else
					{
						// Mip 0 of layer 0: what a render target is, and the first of an array.
						const bool transition = state != resource_usage::copy_source;
						const resource barrier_resources[] = { source };
						const resource_usage before[] = { state };
						const resource_usage during[] = { resource_usage::copy_source };
						if (transition)
							cmd->barrier(1, barrier_resources, before, during);
						cmd->copy_texture_region(source, 0, nullptr, texture, 0, nullptr);
						if (transition)
							cmd->barrier(1, barrier_resources, during, before);

						m_held.push_back(texture);
						m_held_bytes += bytes;
					}
				}
			}
			out.push_back(record);
		}

		for (size_t i = 0; i < out.size(); ++i)
		{
			out[i].index = static_cast<uint32_t>(i);
			out[i].count = static_cast<uint32_t>(out.size());
		}
		m_held_since = frame_index;

		log::Info("Deep capture: %zu buffer(s) copied out of %zu texture(s) the frame wrote to, %.1f MiB",
			m_held.size(), out.size(), static_cast<double>(m_held_bytes) / (1024.0 * 1024.0));
		return out;
	}

	void CaptureBuffers::Release(uint64_t frame_index)
	{
		for (const resource &texture : m_held)
			m_retired.push_back({ texture, frame_index });
		m_held.clear();
		m_held_bytes = 0;
	}

	void CaptureBuffers::Update(device *device, uint64_t frame_index)
	{
		// A standalone that went away, or never read them: the memory goes back to the game.
		if (!m_held.empty() && frame_index > m_held_since + kReleaseAfterFrames)
		{
			log::Info("Deep capture: releasing %zu buffer(s) nobody read", m_held.size());
			Release(frame_index);
		}

		if (device == nullptr || m_retired.empty())
			return;
		const auto done = std::remove_if(m_retired.begin(), m_retired.end(), [&](const Retired &retired) {
			if (frame_index < retired.frame_index + kRetireFrames)
				return false;
			device->destroy_resource(retired.texture);
			return true;
		});
		m_retired.erase(done, m_retired.end());
	}

	void CaptureBuffers::Shutdown(device *device)
	{
		Release(0);
		if (device != nullptr)
			for (const Retired &retired : m_retired)
				device->destroy_resource(retired.texture);
		m_retired.clear();
	}
}
