// CyGPUInspectorRS — per command list recording state.
//
// A command list is only ever recorded by one thread at a time, so nothing here is locked. The
// events are merged into the frame recorder when the list is executed (D3D12, deferred contexts)
// or at present time (D3D11 immediate context, which ReShade exposes as both queue and list).
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/Protocol.hpp>

#include <cstdint>
#include <vector>

namespace cygi
{
	// A GPU timestamp taken while this list was being recorded. It knows the command by its
	// position in *this list*, which is all there is at recording time; which command of the
	// *frame* that is only exists once the list has been executed and its events merged, and that
	// is when the mark is told (GpuTimer::Attribute). Plain data so this header does not depend on
	// the timer.
	struct TimingMark
	{
		uint32_t slot = 0;
		uint32_t index = 0xFFFFFFFFu;
		uint32_t generation = 0;
		uint32_t local_event = 0;

		bool IsValid() const { return index != 0xFFFFFFFFu; }
	};

	struct __declspec(uuid("cac12a28-7d91-4481-a6ec-7cb3bb8522c7")) CommandListState
	{
		static constexpr uint32_t kMaxRenderTargets = kMaxTrackedRenderTargets;

		uint32_t render_target_ids[kMaxRenderTargets] = {};
		uint32_t render_target_count = 0;
		uint32_t depth_target_id = 0;
		uint32_t graphics_pipeline_id = 0;
		uint32_t compute_pipeline_id = 0;
		// Kept so a replacement can be bound and the original put back afterwards.
		uint64_t graphics_pipeline_native = 0;
		uint64_t compute_pipeline_native = 0;
		uint32_t graphics_pipeline_stages = 0;
		uint32_t compute_pipeline_stages = 0;
		// Last render target set a timestamp was taken for, so pass timing only marks changes.
		uint64_t timed_target_signature = 0;
		uint8_t queue_index = 0;
		bool in_render_pass = false;

		std::vector<FrameEvent> events;
		// The timestamps taken on this list, waiting for it to be executed.
		std::vector<TimingMark> timing_marks;

		// ------------------------------------------------------------------------------
		// Deep capture only. None of this is touched, allocated or looked at in runtime
		// mode: the callbacks that fill it return immediately when no capture is running.
		// ------------------------------------------------------------------------------

		// What is bound right now, as a flat list rather than a map: a draw has a few dozen
		// bindings, and a linear scan over that beats hashing, especially on the game's thread.
		std::vector<DrawBinding> live_bindings;

		float viewport[4] = {};
		float depth_range[2] = {};
		int32_t scissor[4] = {};
		uint32_t viewport_count = 0;
		uint32_t topology = 0;
		uint32_t index_format = 0;
		bool viewport_valid = false;
		bool scissor_valid = false;
		// A descriptor table was bound instead of pushed, so part of the binding list is a guess
		// rather than a reading. The interface says so instead of showing a plausible blank.
		bool tables_unresolved = false;

		struct RecordedDrawState
		{
			DrawStateRecord record;
			uint32_t local_event;     // index within this command list, remapped when merged
			uint32_t binding_offset;  // into binding_pool
		};

		struct RecordedBarrierSet
		{
			BarrierSetRecord record;
			uint32_t local_event;
			uint32_t entry_offset;    // into barrier_pool
		};

		std::vector<RecordedDrawState> draw_states;
		std::vector<DrawBinding> binding_pool;
		std::vector<RecordedBarrierSet> barrier_sets;
		std::vector<BarrierEntry> barrier_pool;

		// Replaces the binding on this slot, or adds it. Slots are identified the way Direct3D
		// identifies them — register file, stage, register index — so rebinding t3 for the pixel
		// shader overwrites the previous t3 instead of piling up.
		void SetBinding(SlotKind kind, ShaderStage stage, uint16_t slot, uint32_t resource_id,
		                uint32_t offset = 0, uint32_t size = 0)
		{
			const uint8_t kind_value = static_cast<uint8_t>(kind);
			const uint8_t stage_value = static_cast<uint8_t>(stage);
			for (DrawBinding &binding : live_bindings)
			{
				if (binding.kind == kind_value && binding.stage == stage_value && binding.slot == slot)
				{
					binding.resource_id = resource_id;
					binding.offset = offset;
					binding.size = size;
					return;
				}
			}

			DrawBinding binding = {};
			binding.kind = kind_value;
			binding.stage = stage_value;
			binding.slot = slot;
			binding.resource_id = resource_id;
			binding.offset = offset;
			binding.size = size;
			live_bindings.push_back(binding);
		}

		// Called on reset_command_list and after the events were merged.
		void Reset()
		{
			ResetBindings();
			events.clear();
			// A list reset without being executed never ran: its marks keep measuring nothing.
			timing_marks.clear();
			ResetCapture();
		}

		void ResetCapture()
		{
			draw_states.clear();
			binding_pool.clear();
			barrier_sets.clear();
			barrier_pool.clear();
		}

		void ResetBindings()
		{
			render_target_count = 0;
			depth_target_id = 0;
			graphics_pipeline_id = 0;
			compute_pipeline_id = 0;
			graphics_pipeline_native = 0;
			compute_pipeline_native = 0;
			graphics_pipeline_stages = 0;
			compute_pipeline_stages = 0;
			timed_target_signature = 0;
			in_render_pass = false;
			for (uint32_t &id : render_target_ids)
				id = 0;

			live_bindings.clear();
			viewport_count = 0;
			topology = 0;
			index_format = 0;
			viewport_valid = false;
			scissor_valid = false;
			tables_unresolved = false;
		}

		FrameEvent &Push(EventKind kind)
		{
			// The index is rewritten when the list is merged into the frame.
			events.emplace_back();
			FrameEvent &event = events.back();
			event.kind = kind;
			event.queue_index = queue_index;
			return event;
		}

		uint32_t PrimaryRenderTarget() const { return render_target_count > 0 ? render_target_ids[0] : 0; }
	};
}
