// CyGPUInspectorRS — GPU preview bridge.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "PreviewBridge.hpp"

#include "Addon/Log.hpp"

#include <CyGPUInspectorCore/Format.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

using namespace reshade::api;

namespace cygi
{
	namespace
	{
		uint32_t MipExtent(uint32_t base, uint32_t level)
		{
			const uint32_t value = base >> level;
			return value != 0 ? value : 1;
		}

		// The resource being previewed, and the state its last recorded barrier left it in. Global
		// rather than per bridge because the barrier callback runs on the game's threads with
		// nothing but a command list in hand, and it has to decide in one load that it has nothing
		// to do. Resource handles are unique in the process, so one pair covers every device.
		std::atomic<uint64_t> g_watched_handle{ 0 };
		std::atomic<uint32_t> g_watched_state{ 0 };
	}

	uint64_t ShareHandleWith(uint64_t handle, uint32_t app_process_id, bool is_nt_handle)
	{
		if (!is_nt_handle || handle == 0)
			return handle;

		HANDLE duplicated = nullptr;
		BOOL ok = FALSE;
		if (app_process_id != 0)
		{
			if (const HANDLE target = OpenProcess(PROCESS_DUP_HANDLE, FALSE, app_process_id))
			{
				ok = DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(handle), target, &duplicated, 0,
					FALSE, DUPLICATE_SAME_ACCESS);
				CloseHandle(target);
			}
		}
		// ReShade hands the NT handle over and keeps no copy: nobody else would ever close it, and
		// an open NT handle keeps the texture's memory alive after the resource is destroyed.
		const DWORD error = GetLastError();
		CloseHandle(reinterpret_cast<HANDLE>(handle));
		SetLastError(error);

		return ok != 0 ? reinterpret_cast<uint64_t>(duplicated) : 0;
	}

	PreviewStatus CreateSharedCopyTarget(device *device, uint32_t width, uint32_t height, uint32_t shared_format,
	                                     bool nt_handle, resource &texture, uint64_t &share_handle)
	{
		texture = {};
		share_handle = 0;

		// `shared` is what makes a resource shared at all; `shared_nt_handle` only says which kind
		// of handle. They are separate bits, and ReShade's D3D12 back end tests `shared`: passing
		// the NT bit alone created an ordinary, unshared texture and no handle, so no D3D12 game
		// ever produced a preview.
		const resource_flags flags = nt_handle ? (resource_flags::shared | resource_flags::shared_nt_handle)
		                                       : resource_flags::shared;
		// Render target usage although nothing is ever rendered into it: a shared texture has to
		// be bindable as both render target and shader resource, or D3D11 refuses to create it
		// (legacy handles) or to open it (NT handles from D3D12, E_INVALIDARG). Formats that cannot
		// be render targets at all — the 24/8 depth family — get a second try without it.
		void *native_share_handle = nullptr;
		const resource_usage full_usage =
			resource_usage::copy_dest | resource_usage::shader_resource | resource_usage::render_target;
		const resource_desc share_desc(resource_type::texture_2d, width, height, 1, 1,
			static_cast<format>(shared_format), 1, memory_heap::default_, full_usage, flags);
		if (!device->create_resource(share_desc, nullptr, resource_usage::copy_dest, &texture, &native_share_handle))
		{
			native_share_handle = nullptr;
			const resource_desc plain_desc(resource_type::texture_2d, width, height, 1, 1,
				static_cast<format>(shared_format), 1, memory_heap::default_,
				resource_usage::copy_dest | resource_usage::shader_resource, flags);
			if (!device->create_resource(plain_desc, nullptr, resource_usage::copy_dest, &texture,
			                             &native_share_handle))
			{
				texture = {};
				return PreviewStatus::creation_failed;
			}
			log::Warning("Sharing: %s cannot be a render target; the viewer may not be able to open it",
				FormatName(shared_format));
		}

		share_handle = reinterpret_cast<uint64_t>(native_share_handle);
		if (share_handle == 0)
		{
			// Created, but not shared: said as such rather than as a duplication failure.
			log::Error("Sharing: the texture was created without a share handle");
			device->destroy_resource(texture);
			texture = {};
			return PreviewStatus::sharing_unsupported;
		}
		return PreviewStatus::ready;
	}

	void PreviewBridge::SetRequest(const PreviewRequest &request)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_request = request;
		m_has_request = true;
		m_request_changed = true;
		// A new request is live. A freeze left over from a capture or from a standalone that has
		// gone would otherwise keep the new viewer on an old image it has no way to know about.
		m_frozen.store(false, std::memory_order_relaxed);
		m_keep_this_frame.store(false, std::memory_order_relaxed);
	}

	void PreviewBridge::ClearRequest()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_has_request = false;
		m_request_changed = true;
	}

	bool PreviewBridge::HasRequest() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_has_request;
	}

	void PreviewBridge::SetFrozen(bool frozen)
	{
		m_frozen.store(frozen, std::memory_order_relaxed);
	}

	void PreviewBridge::KeepThisFrame()
	{
		m_keep_this_frame.store(true, std::memory_order_relaxed);
	}

	void PreviewBridge::NoteBarriers(uint32_t count, const resource *resources, const resource_usage *new_states)
	{
		const uint64_t watched = g_watched_handle.load(std::memory_order_relaxed);
		if (watched == 0 || resources == nullptr || new_states == nullptr)
			return;
		for (uint32_t i = 0; i < count; ++i)
			if (resources[i].handle == watched)
				g_watched_state.store(static_cast<uint32_t>(new_states[i]), std::memory_order_relaxed);
	}

	namespace
	{
		// Frames a game keeps in flight, with a wide margin: past this many updates, every copy
		// recorded into a retired texture has certainly run.
		constexpr uint64_t kRetireUpdates = 8;
	}

	void PreviewBridge::ReleaseTexture(device * /*device*/)
	{
		// Retired, not destroyed: see the header. The share handle the standalone holds keeps the
		// memory alive on its side for as long as it has the texture open.
		if (m_texture.handle != 0)
			m_retired.push_back({ m_texture, m_updates });
		m_texture = {};
		m_shared_handle = 0;
		m_shared_resource_id = 0;
		m_width = 0;
		m_height = 0;
		m_format = 0;
		m_shared_bytes = 0;
	}

	void PreviewBridge::DestroyRetired(device *device, bool all)
	{
		if (device == nullptr)
			return;
		size_t kept = 0;
		for (const Retired &retired : m_retired)
		{
			if (all || m_updates >= retired.update + kRetireUpdates)
				device->destroy_resource(retired.texture);
			else
				m_retired[kept++] = retired;
		}
		m_retired.resize(kept);
	}

	void PreviewBridge::Shutdown(device *device)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_has_request = false;
		ReleaseTexture(device);
		// The device is going away: nothing of it runs any more.
		DestroyRetired(device, true);
	}

	bool PreviewBridge::Update(device *device, command_queue *queue, const ResourceTracker &resources,
	                           resource back_buffer, uint32_t app_process_id, PreviewReadyRecord &out)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (device == nullptr || queue == nullptr)
			return false;

		++m_updates;
		DestroyRetired(device, false);

		if (!m_has_request)
		{
			if (m_request_changed)
			{
				ReleaseTexture(device);
				m_request_changed = false;
				g_watched_handle.store(0, std::memory_order_relaxed);
			}
			return false;
		}

		const PreviewRequest request = m_request;
		const bool changed = m_request_changed;
		m_request_changed = false;

		// Report a failure once per request rather than every frame.
		auto fail = [&](PreviewStatus status) {
			ReleaseTexture(device);
			if (!changed && m_last_status == status)
				return false;
			m_last_status = status;

			out = {};
			out.request_id = request.request_id;
			out.resource_id = request.resource_id;
			out.status = status;
			return true;
		};

		const bool final_image = request.resource_id == kPreviewFinalImage;
		resource source = final_image ? back_buffer : resources.HandleOf(request.resource_id);
		// The host's final image, while it is alive; the swap chain's otherwise.
		uint32_t source_id = final_image ? 0 : request.resource_id;
		if (final_image && m_final_source.handle != 0)
		{
			const uint32_t id = resources.IdOf(m_final_source);
			if (id != 0 && resources.HandleOf(id).handle == m_final_source.handle)
			{
				source = m_final_source;
				source_id = id;
			}
		}
		if (source.handle == 0)
			return fail(PreviewStatus::unknown_resource);

		// Which state the copy has to take the source out of, and put it back into.
		//   the image being presented: `present`, which is what a swap chain buffer is in once
		//   the game has handed it over — assuming "shader resource" here transitioned it from a
		//   state it was not in, and presented it in one it must not be presented in;
		//   anything else: the state its last recorded barrier left it in, and only when no
		//   barrier was seen, the state a finished render target or texture is usually left in.
		ResourceRecord record = {};
		const bool back_buffer_resource = source_id == 0 ||
			(resources.CopyRecord(source_id, record) && (record.usage_flags & kUsageBackBuffer) != 0);
		resource_usage state_at_present = resource_usage::shader_resource;
		bool state_known = true;
		if (back_buffer_resource)
		{
			state_at_present = resource_usage::present;
		}
		else
		{
			if (g_watched_handle.load(std::memory_order_relaxed) != source.handle)
			{
				g_watched_handle.store(source.handle, std::memory_order_relaxed);
				g_watched_state.store(0, std::memory_order_relaxed);
			}
			const uint32_t noted = g_watched_state.load(std::memory_order_relaxed);
			if (noted != 0)
				state_at_present = static_cast<resource_usage>(noted);
			state_known = noted != 0;
		}

		// On Direct3D 12 and Vulkan, a copy from a state the texture is not in is undefined, and
		// the GPU does not report it: it may hang. Nothing is copied until a transition has said
		// where the texture is — for a render target, that is the next frame.
		const device_api api = device->get_api();
		if (!state_known && (api == device_api::d3d12 || api == device_api::vulkan))
			return fail(PreviewStatus::state_unknown);

		const resource_desc desc = device->get_resource_desc(source);
		if (desc.type != resource_type::texture_2d && desc.type != resource_type::surface)
			return fail(PreviewStatus::not_a_texture);
		if (!FormatIsPreviewSupported(static_cast<uint32_t>(desc.texture.format)))
			return fail(PreviewStatus::unsupported_format);
		if (desc.texture.samples > 1)
			return fail(PreviewStatus::multisampled);

		const bool nt_handle = device->check_capability(device_caps::shared_resource_nt_handle);
		if (!nt_handle && !device->check_capability(device_caps::shared_resource))
			return fail(PreviewStatus::sharing_unsupported);

		const uint32_t mip = !final_image && request.mip_level < desc.texture.levels ? request.mip_level : 0;
		const uint32_t slice = !final_image && request.array_slice < desc.texture.depth_or_layers
			? request.array_slice : 0;
		const uint32_t width = MipExtent(desc.texture.width, mip);
		const uint32_t height = MipExtent(desc.texture.height, mip);
		const uint32_t shared_format = FormatShareableCopyTarget(static_cast<uint32_t>(desc.texture.format));

		// (Re)create the shared texture when the geometry of the preview changed. A shared texture
		// must stay a single mip, single layer 2D surface: legacy D3D11 share handles require it.
		const bool needs_new_texture = m_texture.handle == 0 || m_width != width || m_height != height ||
			m_format != shared_format || m_shared_resource_id != request.resource_id || m_mip != mip ||
			m_slice != slice;

		if (needs_new_texture)
		{
			ReleaseTexture(device);

			uint64_t local_handle = 0;
			const PreviewStatus created = CreateSharedCopyTarget(device, width, height, shared_format, nt_handle,
				m_texture, local_handle);
			if (created != PreviewStatus::ready)
				return fail(created);
			const uint64_t app_handle = ShareHandleWith(local_handle, app_process_id, nt_handle);
			if (app_handle == 0)
			{
				log::Error("Preview: the share handle could not be duplicated into the standalone (error %lu)",
					GetLastError());
				return fail(PreviewStatus::handle_duplication_failed);
			}

			m_shared_handle = app_handle;
			m_shared_resource_id = request.resource_id;
			m_width = width;
			m_height = height;
			m_format = shared_format;
			m_mip = mip;
			m_slice = slice;
			m_is_nt_handle = nt_handle;

			const uint32_t bytes_per_pixel = FormatBytesPerPixel(shared_format);
			m_shared_bytes = width * height * (bytes_per_pixel != 0 ? bytes_per_pixel : 4);

			if (final_image)
				log::Info("Preview: sharing the final image as %ux%u %s", width, height, FormatName(shared_format));
			else
				log::Info("Preview: sharing resource #%u as %ux%u %s", request.resource_id, width, height,
					FormatName(shared_format));
		}

		// Frozen: the image already shared stays as it is. A new texture still gets its first copy,
		// or there would be nothing to look at.
		const bool keep = m_keep_this_frame.exchange(false, std::memory_order_relaxed);
		if (m_frozen.load(std::memory_order_relaxed) && !needs_new_texture && !keep)
			return false;
		if (keep)
			m_frozen.store(true, std::memory_order_relaxed);

		// Refresh the content every frame. The copy is recorded on the game's own immediate list,
		// so it costs one GPU copy and no CPU synchronisation.
		command_list *cmd = queue->get_immediate_command_list();
		if (cmd == nullptr)
			return fail(PreviewStatus::creation_failed);

		const uint32_t subresource = mip + slice * desc.texture.levels;

		// D3D12 and Vulkan need explicit states, worked out above; D3D11 ignores barriers.
		const resource barrier_resources[] = { source };
		const resource_usage before[] = { state_at_present };
		const resource_usage during[] = { resource_usage::copy_source };
		cmd->barrier(1, barrier_resources, before, during);

		cmd->copy_texture_region(source, subresource, nullptr, m_texture, 0, nullptr);

		cmd->barrier(1, barrier_resources, during, before);

		m_last_status = PreviewStatus::ready;
		if (!needs_new_texture)
			return false;

		out = {};
		out.request_id = request.request_id;
		out.resource_id = request.resource_id;
		out.status = PreviewStatus::ready;
		out.shared_handle = m_shared_handle;
		out.width = m_width;
		out.height = m_height;
		out.format = m_format;
		out.is_nt_handle = m_is_nt_handle ? 1u : 0u;
		return true;
	}
}
