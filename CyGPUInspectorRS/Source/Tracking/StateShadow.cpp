// CyGPUInspectorRS — shadowing the Direct3D state, for the deep capture only.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "StateShadow.hpp"

#include <cstring>

using namespace reshade::api;

namespace cygi
{
	namespace
	{
		// A binding call names the stages it applies to as a mask. The capture stores one stage
		// per slot, because that is how Direct3D 11 addresses a register and how the interface
		// shows it; a call that covers several stages is recorded once per stage.
		constexpr ShaderStage kStages[] = {
			ShaderStage::vertex, ShaderStage::hull, ShaderStage::domain, ShaderStage::geometry,
			ShaderStage::pixel, ShaderStage::compute, ShaderStage::amplification, ShaderStage::mesh,
		};
		constexpr shader_stage kStageBits[] = {
			shader_stage::vertex, shader_stage::hull, shader_stage::domain, shader_stage::geometry,
			shader_stage::pixel, shader_stage::compute, shader_stage::amplification, shader_stage::mesh,
		};

		SlotKind SlotKindOf(descriptor_type type)
		{
			switch (type)
			{
			case descriptor_type::sampler:
				return SlotKind::sampler;
			case descriptor_type::unordered_access_view:
			case descriptor_type::buffer_unordered_access_view:
				return SlotKind::unordered_access;
			case descriptor_type::constant_buffer:
				return SlotKind::constant_buffer;
			default:
				return SlotKind::shader_resource;
			}
		}

		// Applies one descriptor to one slot, for every stage the call covers.
		template <typename Apply>
		void ForEachStage(CommandListState &state, shader_stage stages, Apply apply)
		{
			const auto mask = static_cast<uint32_t>(stages);
			bool any = false;
			for (size_t i = 0; i < std::size(kStages); ++i)
			{
				if ((mask & static_cast<uint32_t>(kStageBits[i])) == 0)
					continue;
				any = true;
				apply(kStages[i]);
			}
			// `shader_stage::all` and the ray tracing stages do not match any of the graphics bits
			// above; rather than drop the binding, record it once without a stage.
			if (!any)
				apply(ShaderStage::unknown);
			(void)state;
		}
	}

	void ApplyDescriptorUpdate(CommandListState &state, const ResourceTracker &resources,
	                           shader_stage stages, const descriptor_table_update &update)
	{
		if (update.descriptors == nullptr || update.count == 0)
			return;

		const SlotKind kind = SlotKindOf(update.type);

		for (uint32_t i = 0; i < update.count; ++i)
		{
			const auto slot = static_cast<uint16_t>(update.binding + i);
			uint32_t resource_id = 0;
			uint32_t offset = 0;
			uint32_t size = 0;

			switch (update.type)
			{
			case descriptor_type::sampler:
				// A sampler is not a resource: it has no id in our tables, and showing the raw
				// handle would be noise. The slot is recorded so the interface can say a sampler
				// was bound there.
				break;
			case descriptor_type::sampler_with_resource_view:
			{
				const auto *entries = static_cast<const sampler_with_resource_view *>(update.descriptors);
				resource_id = resources.IdOfView(entries[i].view);
				// Direct3D 11 binds the sampler and the view on the same slot number; record the
				// sampler side too so the interface can show both.
				state.SetBinding(SlotKind::sampler, ShaderStage::unknown, slot, 0);
				break;
			}
			case descriptor_type::constant_buffer:
			{
				const auto *ranges = static_cast<const buffer_range *>(update.descriptors);
				resource_id = resources.IdOf(ranges[i].buffer);
				offset = static_cast<uint32_t>(ranges[i].offset);
				size = ranges[i].size == UINT64_MAX ? 0 : static_cast<uint32_t>(ranges[i].size);
				break;
			}
			case descriptor_type::buffer_shader_resource_view:
			case descriptor_type::buffer_unordered_access_view:
			case descriptor_type::shader_resource_view:
			case descriptor_type::unordered_access_view:
			default:
			{
				const auto *views = static_cast<const resource_view *>(update.descriptors);
				resource_id = resources.IdOfView(views[i]);
				break;
			}
			}

			ForEachStage(state, stages, [&](ShaderStage stage) {
				state.SetBinding(kind, stage, slot, resource_id, offset, size);
			});
		}
	}

	void ApplyVertexBuffers(CommandListState &state, const ResourceTracker &resources, uint32_t first,
	                        uint32_t count, const resource *buffers, const uint64_t *offsets)
	{
		if (buffers == nullptr)
			return;

		for (uint32_t i = 0; i < count; ++i)
		{
			state.SetBinding(SlotKind::vertex_buffer, ShaderStage::unknown,
				static_cast<uint16_t>(first + i), resources.IdOf(buffers[i]),
				offsets != nullptr ? static_cast<uint32_t>(offsets[i]) : 0);
		}
	}

	void ApplyIndexBuffer(CommandListState &state, const ResourceTracker &resources, resource buffer,
	                      uint64_t offset, uint32_t index_size)
	{
		state.index_format = index_size;
		state.SetBinding(SlotKind::index_buffer, ShaderStage::unknown, 0, resources.IdOf(buffer),
			static_cast<uint32_t>(offset), index_size);
	}

	void ApplyViewports(CommandListState &state, uint32_t first, uint32_t count,
	                    const viewport *viewports)
	{
		state.viewport_count = first + count;
		// Only viewport 0 is recorded: everything else is a rounding error in practice, and a
		// draw state record with eight viewports in it would be mostly zeroes.
		if (first != 0 || count == 0 || viewports == nullptr)
			return;

		state.viewport[0] = viewports[0].x;
		state.viewport[1] = viewports[0].y;
		state.viewport[2] = viewports[0].width;
		state.viewport[3] = viewports[0].height;
		state.depth_range[0] = viewports[0].min_depth;
		state.depth_range[1] = viewports[0].max_depth;
		state.viewport_valid = true;
	}

	void ApplyScissorRects(CommandListState &state, uint32_t first, uint32_t count, const rect *rects)
	{
		if (first != 0 || count == 0 || rects == nullptr)
			return;

		state.scissor[0] = rects[0].left;
		state.scissor[1] = rects[0].top;
		state.scissor[2] = rects[0].right;
		state.scissor[3] = rects[0].bottom;
		state.scissor_valid = true;
	}

	void ApplyPipelineStates(CommandListState &state, uint32_t count, const dynamic_state *states,
	                         const uint32_t *values)
	{
		if (states == nullptr || values == nullptr)
			return;

		for (uint32_t i = 0; i < count; ++i)
		{
			// Only the topology is kept per command. The rest of the dynamic state belongs to the
			// pipeline record, which is sent once instead of once per draw.
			if (states[i] == dynamic_state::primitive_topology)
				state.topology = values[i];
		}
	}

	void RecordDrawState(CommandListState &state, DeepCapture &capture, bool compute,
	                     uint32_t stage_mask)
	{
		if (state.events.empty())
			return;
		if (!capture.TakeDrawStateBudget())
			return;

		CommandListState::RecordedDrawState recorded = {};
		recorded.local_event = static_cast<uint32_t>(state.events.size()) - 1;
		recorded.binding_offset = static_cast<uint32_t>(state.binding_pool.size());

		DrawStateRecord &record = recorded.record;
		record.pipeline_id = compute ? state.compute_pipeline_id : state.graphics_pipeline_id;
		record.topology = state.topology;
		record.index_format = state.index_format;
		record.viewport_count = state.viewport_count;
		record.stage_mask = stage_mask;

		if (state.viewport_valid)
		{
			std::memcpy(record.viewport, state.viewport, sizeof(record.viewport));
			std::memcpy(record.depth_range, state.depth_range, sizeof(record.depth_range));
			record.flags |= kDrawStateViewportValid;
		}
		if (state.scissor_valid)
		{
			std::memcpy(record.scissor, state.scissor, sizeof(record.scissor));
			record.flags |= kDrawStateScissorValid;
		}
		if (state.tables_unresolved)
			record.flags |= kDrawStateTablesUnresolved;

		uint32_t written = 0;
		for (const DrawBinding &binding : state.live_bindings)
		{
			if (written >= kMaxBindingsPerDraw)
			{
				capture.CountDropped(static_cast<uint32_t>(state.live_bindings.size()) - written);
				break;
			}
			// A compute dispatch is not interested in the vertex pipeline's leftovers, and vice
			// versa: keeping them would make every draw look like it bound thirty things.
			const auto stage = static_cast<ShaderStage>(binding.stage);
			if (compute && (stage == ShaderStage::vertex || stage == ShaderStage::pixel ||
			                stage == ShaderStage::geometry || stage == ShaderStage::hull ||
			                stage == ShaderStage::domain))
				continue;
			if (!compute && stage == ShaderStage::compute)
				continue;

			state.binding_pool.push_back(binding);
			++written;
		}

		// The render targets are state too, and the interface should show them in the same list
		// as everything else the draw was given.
		if (!compute)
		{
			for (uint32_t i = 0; i < state.render_target_count && written < kMaxBindingsPerDraw; ++i)
			{
				DrawBinding target = {};
				target.kind = static_cast<uint8_t>(SlotKind::render_target);
				target.stage = static_cast<uint8_t>(ShaderStage::unknown);
				target.slot = static_cast<uint16_t>(i);
				target.resource_id = state.render_target_ids[i];
				state.binding_pool.push_back(target);
				++written;
			}
			if (state.depth_target_id != 0 && written < kMaxBindingsPerDraw)
			{
				DrawBinding depth = {};
				depth.kind = static_cast<uint8_t>(SlotKind::depth_stencil);
				depth.stage = static_cast<uint8_t>(ShaderStage::unknown);
				depth.resource_id = state.depth_target_id;
				state.binding_pool.push_back(depth);
				++written;
			}
		}

		record.binding_count = written;
		capture.CountBindings(written);
		state.draw_states.push_back(recorded);
	}

	void RecordBarriers(CommandListState &state, DeepCapture &capture, const ResourceTracker &resources,
	                    uint32_t count, const resource *resources_in, const resource_usage *old_states,
	                    const resource_usage *new_states)
	{
		if (state.events.empty() || count == 0 || resources_in == nullptr)
			return;

		CommandListState::RecordedBarrierSet recorded = {};
		recorded.local_event = static_cast<uint32_t>(state.events.size()) - 1;
		recorded.entry_offset = static_cast<uint32_t>(state.barrier_pool.size());

		for (uint32_t i = 0; i < count; ++i)
		{
			BarrierEntry entry = {};
			entry.resource_id = resources.IdOf(resources_in[i]);
			entry.old_state = old_states != nullptr ? static_cast<uint32_t>(old_states[i]) : 0;
			entry.new_state = new_states != nullptr ? static_cast<uint32_t>(new_states[i]) : 0;
			state.barrier_pool.push_back(entry);
		}

		recorded.record.count = count;
		capture.CountBarriers(count);
		state.barrier_sets.push_back(recorded);
	}

	bool ExtractPipelineState(uint32_t pipeline_id, uint32_t subobject_count,
	                          const pipeline_subobject *subobjects, PipelineStateRecord &out)
	{
		out = {};
		out.pipeline_id = pipeline_id;
		// `known_fields` exists because a pipeline reports only the subobjects it has. Without it
		// the interface could not tell "depth test off" from "the API never said".
		uint32_t known = 0;

		for (uint32_t i = 0; i < subobject_count; ++i)
		{
			const pipeline_subobject &subobject = subobjects[i];
			if (subobject.data == nullptr || subobject.count == 0)
				continue;

			switch (subobject.type)
			{
			case pipeline_subobject_type::blend_state:
			{
				const auto *blend = static_cast<const blend_desc *>(subobject.data);
				for (uint32_t rt = 0; rt < 8; ++rt)
					if (blend->blend_enable[rt])
						out.blend_enable_mask |= 1u << rt;
				out.source_blend = static_cast<uint32_t>(blend->source_color_blend_factor[0]);
				out.dest_blend = static_cast<uint32_t>(blend->dest_color_blend_factor[0]);
				out.blend_op = static_cast<uint32_t>(blend->color_blend_op[0]);
				out.render_target_write_mask = blend->render_target_write_mask[0];
				out.alpha_to_coverage = blend->alpha_to_coverage_enable ? 1u : 0u;
				known |= 1u << 0;
				break;
			}
			case pipeline_subobject_type::rasterizer_state:
			{
				const auto *raster = static_cast<const rasterizer_desc *>(subobject.data);
				out.cull_mode = static_cast<uint32_t>(raster->cull_mode);
				out.fill_mode = static_cast<uint32_t>(raster->fill_mode);
				out.front_counter_clockwise = raster->front_counter_clockwise ? 1u : 0u;
				known |= 1u << 1;
				break;
			}
			case pipeline_subobject_type::depth_stencil_state:
			{
				const auto *depth = static_cast<const depth_stencil_desc *>(subobject.data);
				out.depth_enable = depth->depth_enable ? 1u : 0u;
				out.depth_write = depth->depth_write_mask ? 1u : 0u;
				out.depth_func = static_cast<uint32_t>(depth->depth_func);
				out.stencil_enable = depth->stencil_enable ? 1u : 0u;
				known |= 1u << 2;
				break;
			}
			case pipeline_subobject_type::primitive_topology:
				out.topology = static_cast<uint32_t>(*static_cast<const primitive_topology *>(subobject.data));
				known |= 1u << 3;
				break;
			case pipeline_subobject_type::render_target_formats:
			{
				const auto *formats = static_cast<const format *>(subobject.data);
				const uint32_t count = subobject.count < kMaxTrackedRenderTargets
					? subobject.count : kMaxTrackedRenderTargets;
				for (uint32_t rt = 0; rt < count; ++rt)
					out.render_target_formats[rt] = static_cast<uint32_t>(formats[rt]);
				known |= 1u << 4;
				break;
			}
			case pipeline_subobject_type::depth_stencil_format:
				out.depth_stencil_format = static_cast<uint32_t>(*static_cast<const format *>(subobject.data));
				known |= 1u << 5;
				break;
			case pipeline_subobject_type::sample_mask:
				out.sample_mask = *static_cast<const uint32_t *>(subobject.data);
				known |= 1u << 6;
				break;
			case pipeline_subobject_type::sample_count:
				out.sample_count = *static_cast<const uint32_t *>(subobject.data);
				known |= 1u << 7;
				break;
			default:
				break;
			}
		}

		out.known_fields = known;
		return known != 0;
	}
}
