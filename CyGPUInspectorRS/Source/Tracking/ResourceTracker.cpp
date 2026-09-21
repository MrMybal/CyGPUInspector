// CyGPUInspectorRS — resource tracking.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "ResourceTracker.hpp"

namespace cygi
{
	namespace
	{
		bool HasUsage(reshade::api::resource_usage value, reshade::api::resource_usage flag)
		{
			return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
		}
	}

	ResourceKind ToResourceKind(reshade::api::resource_type type)
	{
		switch (type)
		{
		case reshade::api::resource_type::buffer: return ResourceKind::buffer;
		case reshade::api::resource_type::texture_1d: return ResourceKind::texture_1d;
		case reshade::api::resource_type::texture_2d: return ResourceKind::texture_2d;
		case reshade::api::resource_type::texture_3d: return ResourceKind::texture_3d;
		case reshade::api::resource_type::surface: return ResourceKind::surface;
		case reshade::api::resource_type::unknown:
		default: return ResourceKind::unknown;
		}
	}

	uint32_t ToUsageFlags(reshade::api::resource_usage usage)
	{
		using reshade::api::resource_usage;

		uint32_t flags = kUsageNone;
		if (HasUsage(usage, resource_usage::render_target)) flags |= kUsageRenderTarget;
		if (HasUsage(usage, resource_usage::depth_stencil)) flags |= kUsageDepthStencil;
		if (HasUsage(usage, resource_usage::shader_resource)) flags |= kUsageShaderResource;
		if (HasUsage(usage, resource_usage::unordered_access)) flags |= kUsageUnorderedAccess;
		if (HasUsage(usage, resource_usage::index_buffer)) flags |= kUsageIndexBuffer;
		if (HasUsage(usage, resource_usage::vertex_buffer)) flags |= kUsageVertexBuffer;
		if (HasUsage(usage, resource_usage::constant_buffer)) flags |= kUsageConstantBuffer;
		if (HasUsage(usage, resource_usage::indirect_argument)) flags |= kUsageIndirectArgument;
		if (HasUsage(usage, resource_usage::copy_source)) flags |= kUsageCopySource;
		if (HasUsage(usage, resource_usage::copy_dest)) flags |= kUsageCopyDest;
		if (HasUsage(usage, resource_usage::resolve_source)) flags |= kUsageResolveSource;
		if (HasUsage(usage, resource_usage::resolve_dest)) flags |= kUsageResolveDest;
		return flags;
	}

	uint32_t ResourceTracker::Register(reshade::api::device *device, reshade::api::resource resource,
	                                   const reshade::api::resource_desc &desc, uint64_t frame_index,
	                                   uint32_t event_index)
	{
		if (resource.handle == 0)
			return 0;

		std::unique_lock<std::shared_mutex> lock(m_mutex);

		const auto existing = m_by_handle.find(resource.handle);
		if (existing != m_by_handle.end())
		{
			// The API recycled a handle: refresh the record rather than leak a second id.
			ResourceRecord &record = m_resources[existing->second - 1];
			record.alive = true;
			return record.id;
		}

		ResourceRecord record;
		record.id = static_cast<uint32_t>(m_resources.size()) + 1;
		record.native_handle = resource.handle;
		record.kind = ToResourceKind(desc.type);
		record.usage_flags = ToUsageFlags(desc.usage);
		record.created_frame = frame_index;
		record.created_event = event_index;

		if (desc.type == reshade::api::resource_type::buffer)
		{
			record.buffer_size = desc.buffer.size;
		}
		else
		{
			record.format = static_cast<uint32_t>(desc.texture.format);
			record.width = desc.texture.width;
			record.height = desc.texture.height;
			record.depth_or_layers = desc.texture.depth_or_layers;
			record.mip_levels = desc.texture.levels;
			record.samples = desc.texture.samples;
		}

		if (static_cast<uint32_t>(desc.flags) &
		    (static_cast<uint32_t>(reshade::api::resource_flags::shared) |
		     static_cast<uint32_t>(reshade::api::resource_flags::shared_nt_handle)))
			record.usage_flags |= kUsageShared;

		(void)device;

		m_resources.push_back(record);
		m_by_handle.emplace(resource.handle, record.id);
		m_pending.push_back(record.id);
		return record.id;
	}

	void ResourceTracker::Unregister(reshade::api::resource resource, uint64_t frame_index,
	                                 uint32_t event_index)
	{
		if (resource.handle == 0)
			return;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		const auto it = m_by_handle.find(resource.handle);
		if (it == m_by_handle.end())
			return;

		const uint32_t id = it->second;
		m_by_handle.erase(it);
		if (id != 0 && id <= m_resources.size())
		{
			// The record stays: events of previous frames still refer to this id.
			ResourceRecord &record = m_resources[id - 1];
			record.alive = false;
			record.destroyed_frame = frame_index;
			record.destroyed_event = event_index;
			m_destroyed.push_back(id);
		}
	}

	void ResourceTracker::RegisterView(reshade::api::resource_view view, reshade::api::resource resource)
	{
		if (view.handle == 0)
			return;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		m_view_to_resource[view.handle] = resource.handle;
	}

	void ResourceTracker::UnregisterView(reshade::api::resource_view view)
	{
		if (view.handle == 0)
			return;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		m_view_to_resource.erase(view.handle);
	}

	uint32_t ResourceTracker::IdOf(reshade::api::resource resource) const
	{
		if (resource.handle == 0)
			return 0;

		std::shared_lock<std::shared_mutex> lock(m_mutex);
		const auto it = m_by_handle.find(resource.handle);
		return it != m_by_handle.end() ? it->second : 0;
	}

	uint32_t ResourceTracker::IdOfView(reshade::api::resource_view view) const
	{
		if (view.handle == 0)
			return 0;

		std::shared_lock<std::shared_mutex> lock(m_mutex);
		const auto view_it = m_view_to_resource.find(view.handle);
		if (view_it == m_view_to_resource.end())
			return 0;
		const auto it = m_by_handle.find(view_it->second);
		return it != m_by_handle.end() ? it->second : 0;
	}

	reshade::api::resource ResourceTracker::ResourceOfView(reshade::api::resource_view view) const
	{
		if (view.handle == 0)
			return reshade::api::resource{ 0 };

		std::shared_lock<std::shared_mutex> lock(m_mutex);
		const auto it = m_view_to_resource.find(view.handle);
		return reshade::api::resource{ it != m_view_to_resource.end() ? it->second : 0 };
	}

	bool ResourceTracker::CopyRecord(uint32_t resource_id, ResourceRecord &out) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		if (resource_id == 0 || resource_id > m_resources.size())
			return false;
		out = m_resources[resource_id - 1];
		return true;
	}

	reshade::api::resource ResourceTracker::HandleOf(uint32_t resource_id) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		if (resource_id == 0 || resource_id > m_resources.size())
			return reshade::api::resource{ 0 };

		const ResourceRecord &record = m_resources[resource_id - 1];
		// A destroyed resource keeps its record, but its handle must never be used again.
		return reshade::api::resource{ record.alive ? record.native_handle : 0 };
	}

	void ResourceTracker::MarkBackBuffer(reshade::api::resource resource)
	{
		if (resource.handle == 0)
			return;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		const auto it = m_by_handle.find(resource.handle);
		if (it == m_by_handle.end())
			return;

		ResourceRecord &record = m_resources[it->second - 1];
		if ((record.usage_flags & kUsageBackBuffer) != 0)
			return;

		record.usage_flags |= kUsageBackBuffer;
		m_pending.push_back(record.id); // republish so the standalone learns about the flag
	}

	std::vector<ResourceRecord> ResourceTracker::TakePendingResources()
	{
		std::vector<ResourceRecord> result;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		result.reserve(m_pending.size());
		for (uint32_t id : m_pending)
		{
			if (id == 0 || id > m_resources.size())
				continue;
			result.push_back(m_resources[id - 1]);
		}
		m_pending.clear();
		return result;
	}

	std::vector<ResourceRecord> ResourceTracker::TakeDestroyedResources()
	{
		std::vector<ResourceRecord> result;

		std::unique_lock<std::shared_mutex> lock(m_mutex);
		result.reserve(m_destroyed.size());
		for (uint32_t id : m_destroyed)
		{
			if (id == 0 || id > m_resources.size())
				continue;
			result.push_back(m_resources[id - 1]);
		}
		m_destroyed.clear();
		return result;
	}

	void ResourceTracker::QueueEverything()
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		m_pending.clear();
		for (const ResourceRecord &record : m_resources)
			if (record.alive)
				m_pending.push_back(record.id);
	}

	uint32_t ResourceTracker::ResourceCount() const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_resources.size());
	}

	uint32_t ResourceTracker::AliveCount() const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return static_cast<uint32_t>(m_by_handle.size());
	}
}
