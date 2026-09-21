// CyGPUInspector — protocol helpers (names and shared object naming).
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorCore/Protocol.hpp"

#include <cstdio>

namespace cygi
{
	const char *GraphicsApiName(GraphicsApi api)
	{
		switch (api)
		{
		case GraphicsApi::d3d9: return "D3D9";
		case GraphicsApi::d3d10: return "D3D10";
		case GraphicsApi::d3d11: return "D3D11";
		case GraphicsApi::d3d12: return "D3D12";
		case GraphicsApi::opengl: return "OpenGL";
		case GraphicsApi::vulkan: return "Vulkan";
		case GraphicsApi::unknown:
		default: return "Unknown";
		}
	}

	const char *ShaderStageName(ShaderStage stage)
	{
		switch (stage)
		{
		case ShaderStage::vertex: return "Vertex";
		case ShaderStage::hull: return "Hull";
		case ShaderStage::domain: return "Domain";
		case ShaderStage::geometry: return "Geometry";
		case ShaderStage::pixel: return "Pixel";
		case ShaderStage::compute: return "Compute";
		case ShaderStage::amplification: return "Amplification";
		case ShaderStage::mesh: return "Mesh";
		case ShaderStage::raygen: return "Ray Generation";
		case ShaderStage::any_hit: return "Any Hit";
		case ShaderStage::closest_hit: return "Closest Hit";
		case ShaderStage::miss: return "Miss";
		case ShaderStage::intersection: return "Intersection";
		case ShaderStage::callable: return "Callable";
		case ShaderStage::unknown:
		default: return "Unknown";
		}
	}

	const char *ShaderFormatName(ShaderFormat format)
	{
		switch (format)
		{
		case ShaderFormat::dxbc: return "DXBC";
		case ShaderFormat::dxil: return "DXIL";
		case ShaderFormat::spirv: return "SPIR-V";
		case ShaderFormat::glsl_source: return "GLSL";
		case ShaderFormat::unknown:
		default: return "Unknown";
		}
	}

	const char *TrackingLevelName(TrackingLevel level)
	{
		switch (level)
		{
		case TrackingLevel::idle: return "Idle";
		case TrackingLevel::tracking: return "Tracking";
		case TrackingLevel::pass_timing: return "Pass Timing";
		case TrackingLevel::capture: return "Capture";
		case TrackingLevel::full_draw_timing: return "Full Draw Timing";
		default: return "Unknown";
		}
	}

	const char *CaptureModeName(CaptureMode mode)
	{
		switch (mode)
		{
		case CaptureMode::runtime: return "Runtime";
		case CaptureMode::deep: return "Deep";
		default: return "Unknown";
		}
	}

	const char *CaptureStageName(CaptureStage stage)
	{
		switch (stage)
		{
		case CaptureStage::idle: return "Idle";
		case CaptureStage::armed: return "Armed";
		case CaptureStage::capturing: return "Capturing";
		case CaptureStage::finished: return "Finished";
		case CaptureStage::failed: return "Failed";
		default: return "Unknown";
		}
	}

	const char *SlotKindName(SlotKind kind)
	{
		switch (kind)
		{
		case SlotKind::constant_buffer: return "Constant buffer";
		case SlotKind::shader_resource: return "Shader resource";
		case SlotKind::unordered_access: return "Unordered access";
		case SlotKind::sampler: return "Sampler";
		case SlotKind::vertex_buffer: return "Vertex buffer";
		case SlotKind::index_buffer: return "Index buffer";
		case SlotKind::render_target: return "Render target";
		case SlotKind::depth_stencil: return "Depth stencil";
		default: return "Unknown";
		}
	}

	const char *ResourceKindName(ResourceKind kind)
	{
		switch (kind)
		{
		case ResourceKind::buffer: return "Buffer";
		case ResourceKind::texture_1d: return "Texture1D";
		case ResourceKind::texture_2d: return "Texture2D";
		case ResourceKind::texture_3d: return "Texture3D";
		case ResourceKind::surface: return "Surface";
		case ResourceKind::unknown:
		default: return "Unknown";
		}
	}

	const char *EventKindName(EventKind kind)
	{
		switch (kind)
		{
		case EventKind::draw: return "Draw";
		case EventKind::draw_indexed: return "DrawIndexed";
		case EventKind::draw_indirect: return "DrawIndirect";
		case EventKind::dispatch: return "Dispatch";
		case EventKind::dispatch_indirect: return "DispatchIndirect";
		case EventKind::dispatch_mesh: return "DispatchMesh";
		case EventKind::dispatch_rays: return "DispatchRays";
		case EventKind::copy_resource: return "CopyResource";
		case EventKind::copy_buffer_region: return "CopyBufferRegion";
		case EventKind::copy_texture_region: return "CopyTextureRegion";
		case EventKind::copy_buffer_to_texture: return "CopyBufferToTexture";
		case EventKind::copy_texture_to_buffer: return "CopyTextureToBuffer";
		case EventKind::resolve: return "Resolve";
		case EventKind::clear_render_target: return "ClearRenderTarget";
		case EventKind::clear_depth_stencil: return "ClearDepthStencil";
		case EventKind::clear_unordered_access: return "ClearUnorderedAccess";
		case EventKind::generate_mipmaps: return "GenerateMipmaps";
		case EventKind::barrier: return "Barrier";
		case EventKind::begin_render_pass: return "BeginRenderPass";
		case EventKind::end_render_pass: return "EndRenderPass";
		case EventKind::bind_render_targets: return "BindRenderTargets";
		case EventKind::bind_pipeline: return "BindPipeline";
		case EventKind::present: return "Present";
		case EventKind::none:
		default: return "None";
		}
	}

	const char *PreviewStatusName(PreviewStatus status)
	{
		switch (status)
		{
		case PreviewStatus::ready: return "ready";
		case PreviewStatus::unknown_resource: return "the resource no longer exists in the game";
		case PreviewStatus::not_a_texture: return "only 2D textures can be previewed";
		case PreviewStatus::unsupported_format: return "this format has no Direct3D 11 equivalent";
		case PreviewStatus::multisampled: return "multisampled resources are not previewed yet";
		case PreviewStatus::sharing_unsupported: return "this device cannot create shared resources";
		case PreviewStatus::creation_failed: return "the shared texture could not be created";
		case PreviewStatus::handle_duplication_failed: return "the shared handle could not be duplicated";
		case PreviewStatus::state_unknown: return "its state was never seen in a barrier, so it was not copied";
		case PreviewStatus::over_budget: return "over the capture's buffer budget";
		default: return "unknown";
		}
	}

	const char *McpPermissionName(McpPermission permission)
	{
		switch (permission)
		{
		case McpPermission::debug_control: return "Debug Control";
		case McpPermission::shader_modification: return "Shader Modification";
		case McpPermission::read_only:
		default: return "Read Only";
		}
	}

	void MakeRingName(uint32_t process_id, uint32_t device_index, char *out, size_t out_size)
	{
		std::snprintf(out, out_size, "Local\\CyGPUInspectorRS.%u.%u.events", process_id, device_index);
	}

	void MakeSignalName(uint32_t process_id, uint32_t device_index, char *out, size_t out_size)
	{
		std::snprintf(out, out_size, "Local\\CyGPUInspectorRS.%u.%u.signal", process_id, device_index);
	}

	void MakePipeName(uint32_t process_id, uint32_t device_index, char *out, size_t out_size)
	{
		std::snprintf(out, out_size, "\\\\.\\pipe\\CyGPUInspector\\%u.%u", process_id, device_index);
	}
}
