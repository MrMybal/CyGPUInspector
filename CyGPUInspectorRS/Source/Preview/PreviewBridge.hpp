// CyGPUInspectorRS — GPU preview bridge.
//
// When the standalone asks to look at a resource, the add-on copies it into a *shared* texture on
// the game's own queue and hands the share handle over. The pixels never travel through system
// memory: the standalone opens the same GPU texture and samples it directly.
//
// Everything here runs on the render thread, inside the present event.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Tracking/ResourceTracker.hpp"

#include <reshade.hpp>

#include <CyGPUInspectorCore/Protocol.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace cygi
{
	// Creates a single mip, single layer 2D texture another process can open, and returns the
	// share handle in this process. Used by the preview and by the capture buffers alike; on
	// failure nothing is left allocated.
	PreviewStatus CreateSharedCopyTarget(reshade::api::device *device, uint32_t width, uint32_t height,
	                                     uint32_t shared_format, bool nt_handle,
	                                     reshade::api::resource &texture, uint64_t &share_handle);

	// Hands a share handle over to the standalone. An NT handle is duplicated into its process and
	// the one of this process closed — it keeps the texture's memory alive for as long as it is
	// open, whatever happens to the resource — so this takes ownership of it, failure included.
	// Legacy D3D11 share handles are global, not closable, and come back untouched. 0 on failure.
	uint64_t ShareHandleWith(uint64_t handle, uint32_t app_process_id, bool is_nt_handle);

	class PreviewBridge
	{
	public:
		// Called from the control thread: only stores the request.
		void SetRequest(const PreviewRequest &request);
		void ClearRequest();
		bool HasRequest() const;
		void SetFrozen(bool frozen);
		// Render thread, before Update: the texture that stands for the final image when the host
		// names one (the 3D render of Unreal's main viewport), or {0} for the swap chain's.
		void SetFinalImageSource(reshade::api::resource source) { m_final_source = source; }
		// Copies this frame's image even if frozen, then freezes: called on the last frame of a
		// deep capture, so the image kept is the one of the frame that was captured.
		void KeepThisFrame();

		// Called from every barrier the game records, so it must cost nothing when no preview is
		// running: one atomic load. Remembers the state the previewed resource was last put in,
		// which is what the copy has to transition it from — on D3D12, assuming a state instead is
		// undefined behaviour, and for a back buffer the assumption used to be plain wrong.
		static void NoteBarriers(uint32_t count, const reshade::api::resource *resources,
		                         const reshade::api::resource_usage *new_states);

		// Called on the render thread once per frame. Refreshes the shared texture and fills
		// `out` when the standalone has to be told about a new (or failed) share handle.
		// `back_buffer` is what the swap chain is presenting this frame, for kPreviewFinalImage.
		bool Update(reshade::api::device *device, reshade::api::command_queue *queue,
		            const ResourceTracker &resources, reshade::api::resource back_buffer,
		            uint32_t app_process_id, PreviewReadyRecord &out);

		void Shutdown(reshade::api::device *device);

		uint32_t SharedBytes() const { return m_shared_bytes; }

	private:
		// Retires the shared texture rather than destroying it: the copy into it recorded at the
		// last present may not have run yet, and a GPU writing into a destroyed resource is not an
		// error it reports — it hangs. Retired textures are destroyed a few updates later.
		void ReleaseTexture(reshade::api::device *device);
		void DestroyRetired(reshade::api::device *device, bool all);

		struct Retired
		{
			reshade::api::resource texture;
			uint64_t update;
		};
		std::vector<Retired> m_retired;
		uint64_t m_updates = 0;

		mutable std::mutex m_mutex;
		PreviewRequest m_request = {};
		bool m_has_request = false;
		bool m_request_changed = false;
		std::atomic<bool> m_frozen{ false };
		std::atomic<bool> m_keep_this_frame{ false };
		reshade::api::resource m_final_source = {};

		// The shared texture currently published, if any.
		reshade::api::resource m_texture = {};
		uint64_t m_shared_handle = 0;
		uint32_t m_shared_resource_id = 0;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		uint32_t m_format = 0;
		uint32_t m_mip = 0;
		uint32_t m_slice = 0;
		bool m_is_nt_handle = false;
		uint32_t m_shared_bytes = 0;

		PreviewStatus m_last_status = PreviewStatus::ready;
	};
}
