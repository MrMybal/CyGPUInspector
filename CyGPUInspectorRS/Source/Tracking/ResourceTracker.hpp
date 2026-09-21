// CyGPUInspectorRS — resource tracking.
//
// Follows every resource the game creates (textures and buffers), gives it a stable session id
// (what the UI shows as "Resource #183"), and keeps the view -> resource table needed to resolve
// render target bindings into resource ids.
//
// Resources are created and destroyed from several threads, so the tables are guarded by a
// shared mutex; the hot path only takes the read lock.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <reshade.hpp>

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>
#include <deque>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cygi
{
	struct ResourceRecord
	{
		uint32_t id = 0;
		uint64_t native_handle = 0;
		ResourceKind kind = ResourceKind::unknown;
		uint32_t format = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth_or_layers = 0;
		uint32_t mip_levels = 0;
		uint32_t samples = 0;
		uint32_t usage_flags = 0;
		uint64_t buffer_size = 0;
		uint64_t created_frame = 0;
		uint32_t created_event = 0;
		uint64_t destroyed_frame = 0;
		uint32_t destroyed_event = 0;
		bool alive = true;
	};

	class ResourceTracker
	{
	public:
		// init_resource / destroy_resource.
		uint32_t Register(reshade::api::device *device, reshade::api::resource resource,
		                  const reshade::api::resource_desc &desc, uint64_t frame_index, uint32_t event_index);
		void Unregister(reshade::api::resource resource, uint64_t frame_index, uint32_t event_index);

		// init_resource_view / destroy_resource_view.
		void RegisterView(reshade::api::resource_view view, reshade::api::resource resource);
		void UnregisterView(reshade::api::resource_view view);

		// Hot path lookups, 0 when unknown.
		uint32_t IdOf(reshade::api::resource resource) const;
		uint32_t IdOfView(reshade::api::resource_view view) const;
		reshade::api::resource ResourceOfView(reshade::api::resource_view view) const;

		bool CopyRecord(uint32_t resource_id, ResourceRecord &out) const;
		// Native handle of a still living resource, {0} when it is unknown or already destroyed.
		reshade::api::resource HandleOf(uint32_t resource_id) const;
		// Back buffers are flagged when the swap chain is initialized.
		void MarkBackBuffer(reshade::api::resource resource);

		std::vector<ResourceRecord> TakePendingResources();
		// Resource ids destroyed since the last call, with the frame and event they died in.
		std::vector<ResourceRecord> TakeDestroyedResources();
		void QueueEverything();

		uint32_t ResourceCount() const;
		uint32_t AliveCount() const;

	private:
		mutable std::shared_mutex m_mutex;

		std::vector<ResourceRecord> m_resources;                 // indexed by id - 1
		std::unordered_map<uint64_t, uint32_t> m_by_handle;
		std::unordered_map<uint64_t, uint64_t> m_view_to_resource;

		std::deque<uint32_t> m_pending;
		std::deque<uint32_t> m_destroyed;
	};

	// Translates a ReShade description into the protocol representation.
	ResourceKind ToResourceKind(reshade::api::resource_type type);
	uint32_t ToUsageFlags(reshade::api::resource_usage usage);
}
