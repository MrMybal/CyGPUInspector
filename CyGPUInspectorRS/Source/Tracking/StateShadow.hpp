// CyGPUInspectorRS — shadowing the Direct3D state, for the deep capture only.
//
// The ReShade add-on API reports binding calls as they happen but never the state a command list
// is in. To know what a draw was given, the binding calls have to be followed and remembered. That
// bookkeeping costs something on the game's thread, so none of it runs unless a deep capture is
// armed: every entry point here begins by asking the capture whether it is recording.
//
// It is also where the honest limits of this approach live. Descriptor *tables* (Direct3D 12 root
// tables, Vulkan descriptor sets) are bound by handle, and the add-on API does not hand out the
// contents of a heap, so a table binding is noted as unresolved instead of being guessed at.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "CommandListState.hpp"
#include "DeepCapture.hpp"
#include "ResourceTracker.hpp"

#include <reshade.hpp>

#include <CyGPUInspectorCore/Protocol.hpp>

namespace cygi
{
	// Follows a push_descriptors call into the shadow state.
	void ApplyDescriptorUpdate(CommandListState &state, const ResourceTracker &resources,
	                           reshade::api::shader_stage stages,
	                           const reshade::api::descriptor_table_update &update);

	void ApplyVertexBuffers(CommandListState &state, const ResourceTracker &resources, uint32_t first,
	                        uint32_t count, const reshade::api::resource *buffers,
	                        const uint64_t *offsets);
	void ApplyIndexBuffer(CommandListState &state, const ResourceTracker &resources,
	                      reshade::api::resource buffer, uint64_t offset, uint32_t index_size);
	void ApplyViewports(CommandListState &state, uint32_t first, uint32_t count,
	                    const reshade::api::viewport *viewports);
	void ApplyScissorRects(CommandListState &state, uint32_t first, uint32_t count,
	                       const reshade::api::rect *rects);
	void ApplyPipelineStates(CommandListState &state, uint32_t count,
	                         const reshade::api::dynamic_state *states, const uint32_t *values);

	// Snapshots the shadow state for the command that was just pushed. `local_event` is the index
	// of that command inside this command list; the frame recorder remaps it later.
	void RecordDrawState(CommandListState &state, DeepCapture &capture, bool compute,
	                     uint32_t stage_mask);

	void RecordBarriers(CommandListState &state, DeepCapture &capture, const ResourceTracker &resources,
	                    uint32_t count, const reshade::api::resource *resources_in,
	                    const reshade::api::resource_usage *old_states,
	                    const reshade::api::resource_usage *new_states);

	// Reads the fixed function state out of the subobjects a pipeline was created from. Returns
	// false when the API reported none of it, which is normal for a compute pipeline.
	bool ExtractPipelineState(uint32_t pipeline_id, uint32_t subobject_count,
	                          const reshade::api::pipeline_subobject *subobjects,
	                          PipelineStateRecord &out);
}
