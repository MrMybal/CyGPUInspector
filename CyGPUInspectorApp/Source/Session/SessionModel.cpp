// CyGPUInspectorApp — in memory model of one connected session.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "SessionModel.hpp"

#include <CyGPUInspectorCore/Format.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cygi
{
	namespace
	{
		template <typename T>
		const T *Payload(const uint8_t *payload, uint32_t payload_size)
		{
			return payload_size >= sizeof(T) ? reinterpret_cast<const T *>(payload) : nullptr;
		}

		template <typename Container>
		void EnsureSize(Container &container, uint32_t id)
		{
			if (id > container.size())
				container.resize(id);
		}
	}

	std::string ResourceInfo::Describe() const
	{
		char buffer[256];
		if (kind == ResourceKind::buffer)
			std::snprintf(buffer, sizeof(buffer), "#%u %s %llu bytes", id, ResourceKindName(kind),
				static_cast<unsigned long long>(buffer_size));
		else
			std::snprintf(buffer, sizeof(buffer), "#%u %s %ux%u %s", id, ResourceKindName(kind), width, height,
				FormatName(format));
		return name.empty() ? std::string(buffer) : std::string(buffer) + " \"" + name + "\"";
	}

	void SessionModel::Clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_session = {};
		m_has_session = false;
		m_shaders.clear();
		m_pipelines.clear();
		m_resources.clear();
		m_building = FrameInfo();
		m_last_frame = FrameInfo();
		m_recent_frames.clear();
		m_pending_span = {};
		m_timing_queue.clear();
		m_dropped_timings = 0;
		m_deep_frame = FrameInfo();
		m_stats = {};
		m_preview = {};
		m_preview_pending = false;
		m_timings = FrameTimings();
		m_history.clear();
		m_capture_state = {};
		m_pipeline_states.clear();
		m_records_applied = 0;
		m_bytes_applied = 0;
		m_log.clear();
	}

	void SessionModel::ApplyRecord(const RecordHeader &header, const uint8_t *payload, uint32_t payload_size)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_records_applied;
		m_bytes_applied += header.size;

		switch (header.type)
		{
		case RecordType::session_info:
			if (const SessionInfoRecord *record = Payload<SessionInfoRecord>(payload, payload_size))
			{
				m_session = *record;
				m_has_session = true;
			}
			break;

		case RecordType::shader_code:
			if (const ShaderCodeRecord *record = Payload<ShaderCodeRecord>(payload, payload_size))
			{
				if (record->shader_id == 0)
					break;

				EnsureSize(m_shaders, record->shader_id);
				ShaderInfo &shader = m_shaders[record->shader_id - 1];
				shader.id = record->shader_id;
				shader.stage = record->stage;
				shader.format = record->format;
				shader.shader_model = record->shader_model;
				shader.code_size = record->code_size;
				std::memcpy(shader.signature.bytes.data(), record->signature, shader.signature.bytes.size());
				std::memcpy(shader.semantic_hash.bytes.data(), record->semantic_hash,
					shader.semantic_hash.bytes.size());

				const uint32_t blob_size = payload_size - static_cast<uint32_t>(sizeof(ShaderCodeRecord));
				const uint32_t copy_size = blob_size < record->code_size ? blob_size : record->code_size;
				if (copy_size != 0)
				{
					const uint8_t *code = payload + sizeof(ShaderCodeRecord);
					shader.code.assign(code, code + copy_size);
				}
				shader.first_seen_frame = m_building.index;
			}
			break;

		case RecordType::pipeline_info:
			if (const PipelineInfoRecord *record = Payload<PipelineInfoRecord>(payload, payload_size))
			{
				if (record->pipeline_id == 0)
					break;

				EnsureSize(m_pipelines, record->pipeline_id);
				PipelineInfo &pipeline = m_pipelines[record->pipeline_id - 1];
				pipeline.id = record->pipeline_id;
				pipeline.native_handle = record->native_handle;
				pipeline.stage_mask = record->stage_mask;
				pipeline.is_compute = record->is_compute != 0;

				const uint32_t blob_size = payload_size - static_cast<uint32_t>(sizeof(PipelineInfoRecord));
				const uint32_t available = blob_size / static_cast<uint32_t>(sizeof(uint32_t));
				const uint32_t count = record->shader_count < available ? record->shader_count : available;
				const uint32_t *ids = reinterpret_cast<const uint32_t *>(payload + sizeof(PipelineInfoRecord));
				pipeline.shader_ids.assign(ids, ids + count);
			}
			break;

		case RecordType::resource_info:
			if (const ResourceInfoRecord *record = Payload<ResourceInfoRecord>(payload, payload_size))
			{
				if (record->resource_id == 0)
					break;

				EnsureSize(m_resources, record->resource_id);
				ResourceInfo &resource = m_resources[record->resource_id - 1];
				resource.id = record->resource_id;
				resource.native_handle = record->native_handle;
				resource.kind = record->kind;
				resource.format = record->format;
				resource.width = record->width;
				resource.height = record->height;
				resource.depth_or_layers = record->depth_or_layers;
				resource.mip_levels = record->mip_levels;
				resource.samples = record->samples;
				resource.usage_flags = record->usage_flags;
				resource.buffer_size = record->buffer_size;
				resource.created_frame = record->created_frame;
				resource.created_event = record->created_event;
				resource.alive = true;
			}
			break;

		case RecordType::resource_gone:
			if (const ResourceGoneRecord *record = Payload<ResourceGoneRecord>(payload, payload_size))
			{
				if (ResourceInfo *resource = ResourceById(record->resource_id))
				{
					resource->alive = false;
					resource->destroyed_frame = record->destroyed_frame;
					resource->destroyed_event = record->destroyed_event;
				}
			}
			break;

		case RecordType::frame_begin:
			if (const FrameBeginRecord *record = Payload<FrameBeginRecord>(payload, payload_size))
				BeginFrame(*record);
			break;

		case RecordType::frame_events:
			if (const FrameEventsRecord *record = Payload<FrameEventsRecord>(payload, payload_size))
			{
				const uint32_t blob_size = payload_size - static_cast<uint32_t>(sizeof(FrameEventsRecord));
				if (blob_size / sizeof(FrameEvent) >= record->event_count)
					AppendEvents(*record, reinterpret_cast<const FrameEvent *>(payload + sizeof(FrameEventsRecord)));
			}
			break;

		case RecordType::frame_end:
			if (const FrameEndRecord *record = Payload<FrameEndRecord>(payload, payload_size))
				EndFrame(*record);
			break;

		case RecordType::frame_gpu_span:
			if (const FrameGpuSpanRecord *record = Payload<FrameGpuSpanRecord>(payload, payload_size))
				m_pending_span = *record;
			break;

		case RecordType::timing_results:
			if (const TimingResultsRecord *record = Payload<TimingResultsRecord>(payload, payload_size))
			{
				// The element size is in the record, so an add-on older or newer than this
				// application is read by what it says it sent rather than by what we expect.
				const uint32_t blob_size = payload_size - static_cast<uint32_t>(sizeof(TimingResultsRecord));
				const uint32_t stride = record->result_size != 0 ? record->result_size : kTimingResultSizeV1;
				if (stride >= kTimingResultSizeV1 && static_cast<uint64_t>(blob_size) / stride >= record->count)
				{
					std::vector<TimingResult> results(record->count);
					const uint8_t *blob = payload + sizeof(TimingResultsRecord);
					const size_t copied = std::min<size_t>(stride, sizeof(TimingResult));
					for (uint32_t i = 0; i < record->count; ++i)
						std::memcpy(&results[i], blob + static_cast<size_t>(i) * stride, copied);
					ApplyTimings(*record, results.data(), stride >= sizeof(TimingResult));
				}
			}
			break;

		case RecordType::preview_ready:
			if (const PreviewReadyRecord *record = Payload<PreviewReadyRecord>(payload, payload_size))
			{
				m_preview = *record;
				m_preview_pending = true;
			}
			break;

		case RecordType::stats:
			if (const StatsRecord *record = Payload<StatsRecord>(payload, payload_size))
				m_stats = *record;
			break;

		case RecordType::resource_named:
			if (const ResourceNamedRecord *record = Payload<ResourceNamedRecord>(payload, payload_size))
			{
				if (record->resource_id == 0)
					break;
				const uint32_t blob_size = payload_size - static_cast<uint32_t>(sizeof(ResourceNamedRecord));
				const uint32_t length = std::min(record->name_length, blob_size);
				EnsureSize(m_resources, record->resource_id);
				m_resources[record->resource_id - 1].name.assign(
					reinterpret_cast<const char *>(payload + sizeof(ResourceNamedRecord)), length);
			}
			break;

		case RecordType::capture_scope:
			if (const CaptureScopeRecord *record = Payload<CaptureScopeRecord>(payload, payload_size))
				m_capture_scope = *record;
			break;

		case RecordType::capture_buffer:
			if (const CaptureBufferRecord *record = Payload<CaptureBufferRecord>(payload, payload_size))
				m_capture_buffers.push_back(*record);
			break;

		case RecordType::draw_state:
			if (const DrawStateRecord *record = Payload<DrawStateRecord>(payload, payload_size))
				ApplyDrawState(*record, reinterpret_cast<const DrawBinding *>(
					payload + sizeof(DrawStateRecord)));
			break;

		case RecordType::barrier_set:
			if (const BarrierSetRecord *record = Payload<BarrierSetRecord>(payload, payload_size))
				ApplyBarriers(*record, reinterpret_cast<const BarrierEntry *>(
					payload + sizeof(BarrierSetRecord)));
			break;

		case RecordType::pipeline_state:
			if (const PipelineStateRecord *record = Payload<PipelineStateRecord>(payload, payload_size))
				if (record->pipeline_id != 0)
					m_pipeline_states[record->pipeline_id] = *record;
			break;

		case RecordType::capture_state:
			if (const CaptureStateRecord *record = Payload<CaptureStateRecord>(payload, payload_size))
			{
				// A new capture forgets the scope of the previous one; its own arrives with its frame.
				if (record->stage == CaptureStage::armed)
					m_capture_scope = {};
				m_capture_state = *record;
			}
			break;

		case RecordType::log_message:
			if (const LogMessageRecord *record = Payload<LogMessageRecord>(payload, payload_size))
			{
				const uint32_t blob_size = payload_size - static_cast<uint32_t>(sizeof(LogMessageRecord));
				const uint32_t length = record->text_length < blob_size ? record->text_length : blob_size;
				m_log.emplace_back(reinterpret_cast<const char *>(payload + sizeof(LogMessageRecord)), length);
				if (m_log.size() > 512)
					m_log.erase(m_log.begin());
			}
			break;

		default:
			break;
		}
	}

	void SessionModel::BeginFrame(const FrameBeginRecord &record)
	{
		m_building.events.clear();
		m_building.index = record.frame_index;
		m_building.draw_count = 0;
		m_building.dispatch_count = 0;
		m_building.dropped_events = 0;
	}

	void SessionModel::AppendEvents(const FrameEventsRecord &record, const FrameEvent *events)
	{
		m_building.index = record.frame_index;
		m_building.events.insert(m_building.events.end(), events, events + record.event_count);
	}

	void SessionModel::EndFrame(const FrameEndRecord &record)
	{
		m_building.index = record.frame_index;
		m_building.draw_count = record.draw_count;
		m_building.dispatch_count = record.dispatch_count;
		m_building.dropped_events = record.dropped_events;
		m_building.cpu_frame_ms = record.cpu_frame_ms;
		m_building.addon_cpu_ms = record.addon_cpu_ms;

		// The deep capture records for frame N arrive after frame N was closed, so the frame that
		// is about to be displaced is the one carrying them. Keep it before it goes.
		if (m_last_frame.HasDeepCapture())
			m_deep_frame = m_last_frame;

		// Moved, not copied: keeping a few frames costs their memory, not a copy per frame.
		if (m_last_frame.index != 0 || !m_last_frame.events.empty())
		{
			m_recent_frames.push_back(std::move(m_last_frame));
			while (m_recent_frames.size() > kRecentFrames)
				m_recent_frames.pop_front();
		}

		m_last_frame = std::move(m_building);
		m_building = FrameInfo();
		m_building.events.reserve(m_last_frame.events.size());

		FrameHistoryEntry entry = {};
		entry.index = m_last_frame.index;
		entry.draw_count = m_last_frame.draw_count;
		entry.dispatch_count = m_last_frame.dispatch_count;
		entry.event_count = static_cast<uint32_t>(m_last_frame.events.size());
		entry.cpu_frame_ms = m_last_frame.cpu_frame_ms;
		entry.addon_cpu_ms = m_last_frame.addon_cpu_ms;
		m_history.push_back(entry);
		// A few seconds of history is what a timeline can show and what a person can read; past
		// that it is just memory. The oldest entry goes.
		if (m_history.size() > kMaxFrameHistory)
			m_history.erase(m_history.begin());

		RecomputeFrameStatistics();
	}

	// Deep capture records arrive after the frame they describe has been closed, because the
	// add-on publishes them once the event timeline they index into is already on the wire.
	FrameInfo *SessionModel::FrameForDeepRecord(uint64_t frame_index)
	{
		if (m_last_frame.index == frame_index)
			return &m_last_frame;
		if (m_building.index == frame_index)
			return &m_building;
		return nullptr;
	}

	void SessionModel::ApplyDrawState(const DrawStateRecord &record, const DrawBinding *bindings)
	{
		FrameInfo *frame = FrameForDeepRecord(record.frame_index);
		if (frame == nullptr)
			return;

		CommandState state;
		state.record = record;
		if (bindings != nullptr && record.binding_count != 0)
			state.bindings.assign(bindings, bindings + record.binding_count);

		// Kept sorted by event index so the interface can binary search it while drawing a list
		// of thousands of commands.
		const auto position = std::lower_bound(frame->draw_states.begin(), frame->draw_states.end(),
			record.event_index, [](const CommandState &candidate, uint32_t index) {
				return candidate.record.event_index < index;
			});
		frame->draw_states.insert(position, std::move(state));
	}

	void SessionModel::ApplyBarriers(const BarrierSetRecord &record, const BarrierEntry *entries)
	{
		FrameInfo *frame = FrameForDeepRecord(record.frame_index);
		if (frame == nullptr || entries == nullptr || record.count == 0)
			return;

		BarrierSet set;
		set.event_index = record.event_index;
		set.entries.assign(entries, entries + record.count);

		const auto position = std::lower_bound(frame->barriers.begin(), frame->barriers.end(),
			record.event_index, [](const BarrierSet &candidate, uint32_t index) {
				return candidate.event_index < index;
			});
		frame->barriers.insert(position, std::move(set));
	}

	const PipelineStateRecord *SessionModel::PipelineStateById(uint32_t pipeline_id) const
	{
		const auto found = m_pipeline_states.find(pipeline_id);
		return found != m_pipeline_states.end() ? &found->second : nullptr;
	}

	const CommandState *FrameInfo::DrawStateOfEvent(uint32_t event_index) const
	{
		const auto found = std::lower_bound(draw_states.begin(), draw_states.end(), event_index,
			[](const CommandState &candidate, uint32_t index) {
				return candidate.record.event_index < index;
			});
		return found != draw_states.end() && found->record.event_index == event_index ? &*found : nullptr;
	}

	const BarrierSet *FrameInfo::BarriersOfEvent(uint32_t event_index) const
	{
		const auto found = std::lower_bound(barriers.begin(), barriers.end(), event_index,
			[](const BarrierSet &candidate, uint32_t index) { return candidate.event_index < index; });
		return found != barriers.end() && found->event_index == event_index ? &*found : nullptr;
	}

	void SessionModel::RecomputeFrameStatistics()
	{
		for (ShaderInfo &shader : m_shaders)
		{
			shader.draws_this_frame = 0;
			shader.dispatches_this_frame = 0;
		}
		for (ResourceInfo &resource : m_resources)
		{
			resource.writes_this_frame = 0;
			resource.reads_this_frame = 0;
			resource.first_write_event = 0;
			resource.last_write_event = 0;
			resource.last_read_event = 0;
		}

		for (const FrameEvent &event : m_last_frame.events)
		{
			const bool is_draw = event.kind == EventKind::draw || event.kind == EventKind::draw_indexed ||
			                     event.kind == EventKind::draw_indirect;
			const bool is_dispatch = event.kind == EventKind::dispatch ||
			                         event.kind == EventKind::dispatch_indirect ||
			                         event.kind == EventKind::dispatch_mesh ||
			                         event.kind == EventKind::dispatch_rays;

			if ((is_draw || is_dispatch) && event.pipeline_id != 0 && event.pipeline_id <= m_pipelines.size())
			{
				for (uint32_t shader_id : m_pipelines[event.pipeline_id - 1].shader_ids)
				{
					if (shader_id == 0 || shader_id > m_shaders.size())
						continue;
					ShaderInfo &shader = m_shaders[shader_id - 1];
					if (is_draw)
					{
						++shader.draws_this_frame;
						++shader.total_draws;
					}
					else
					{
						++shader.dispatches_this_frame;
						++shader.total_draws;
					}
				}
			}

			// Binding a render target is not writing to it, and neither is presenting: only a
			// command that actually touches the pixels counts.
			const bool writes = is_draw || is_dispatch ||
				event.kind == EventKind::copy_resource || event.kind == EventKind::copy_buffer_region ||
				event.kind == EventKind::copy_texture_region ||
				event.kind == EventKind::copy_buffer_to_texture ||
				event.kind == EventKind::copy_texture_to_buffer || event.kind == EventKind::resolve ||
				event.kind == EventKind::clear_render_target ||
				event.kind == EventKind::clear_depth_stencil ||
				event.kind == EventKind::clear_unordered_access ||
				event.kind == EventKind::generate_mipmaps;

			// Writes: render target of a draw, destination of a copy / clear / resolve.
			if (writes && event.primary_resource != 0 && event.primary_resource <= m_resources.size())
			{
				ResourceInfo &resource = m_resources[event.primary_resource - 1];
				++resource.writes_this_frame;
				++resource.total_writes;
				if (resource.first_write_event == 0)
					resource.first_write_event = event.index;
				resource.last_write_event = event.index;
			}

			// Reads: source of a copy / resolve, depth target of a draw (read or write).
			if (writes && event.secondary_resource != 0 && event.secondary_resource <= m_resources.size())
			{
				ResourceInfo &resource = m_resources[event.secondary_resource - 1];
				++resource.reads_this_frame;
				resource.last_read_event = event.index;
			}
		}
	}

	bool SessionModel::TakeCaptureBuffers(std::vector<CaptureBufferRecord> &out)
	{
		if (m_capture_buffers.empty())
			return false;
		out.swap(m_capture_buffers);
		m_capture_buffers.clear();
		return true;
	}

	bool SessionModel::TakePreviewUpdate(PreviewReadyRecord &out)
	{
		if (!m_preview_pending)
			return false;
		out = m_preview;
		m_preview_pending = false;
		return true;
	}

	void SessionModel::ApplyTimings(const TimingResultsRecord &record, const TimingResult *results,
	                                bool with_starts)
	{
		m_timings = FrameTimings();
		m_timings.frame_index = record.frame_index;
		m_timings.frequency = m_has_session ? m_session.timestamp_frequency : 0;
		if (m_pending_span.frame_index == record.frame_index)
		{
			m_timings.gpu_begin = m_pending_span.gpu_begin;
			m_timings.gpu_end = m_pending_span.gpu_end;
		}

		for (uint32_t i = 0; i < record.count; ++i)
		{
			m_timings.by_event[results[i].event_index] += results[i].gpu_ticks;
			m_timings.total_ticks += results[i].gpu_ticks;
			if (with_starts)
			{
				// One event can be measured more than once (a command list executed twice);
				// the timeline wants where it first ran.
				const auto found = m_timings.start_by_event.find(results[i].event_index);
				if (found == m_timings.start_by_event.end() || results[i].gpu_start < found->second)
					m_timings.start_by_event[results[i].event_index] = results[i].gpu_start;
			}
		}

		m_timing_queue.push_back(m_timings);
		while (m_timing_queue.size() > kMaxQueuedTimings)
		{
			m_timing_queue.pop_front();
			++m_dropped_timings;
		}

		// The history strip needs the GPU cost of a frame, which only exists once the timestamps
		// come back. Find the frame these belong to and fill it in rather than leaving a gap.
		for (auto entry = m_history.rbegin(); entry != m_history.rend(); ++entry)
		{
			if (entry->index != record.frame_index)
				continue;
			entry->gpu_ms = m_timings.Milliseconds(m_timings.total_ticks);
			break;
		}

		// Attribute the time to shaders through the events of the frame currently held. The two
		// frames are a few apart, so this is an approximation, and the UI says which frame the
		// numbers come from.
		for (ShaderInfo &shader : m_shaders)
			shader.gpu_ticks = 0;

		for (const FrameEvent &event : m_last_frame.events)
		{
			const auto it = m_timings.by_event.find(event.index);
			if (it == m_timings.by_event.end() || event.pipeline_id == 0 ||
			    event.pipeline_id > m_pipelines.size())
				continue;

			for (uint32_t shader_id : m_pipelines[event.pipeline_id - 1].shader_ids)
				if (shader_id != 0 && shader_id <= m_shaders.size())
					m_shaders[shader_id - 1].gpu_ticks += it->second;
		}
	}

	const FrameInfo *SessionModel::FrameByIndex(uint64_t index) const
	{
		if (m_last_frame.index == index)
			return &m_last_frame;
		for (auto frame = m_recent_frames.rbegin(); frame != m_recent_frames.rend(); ++frame)
			if (frame->index == index)
				return &*frame;
		return nullptr;
	}

	std::deque<FrameTimings> SessionModel::TakeQueuedTimings()
	{
		std::deque<FrameTimings> taken;
		taken.swap(m_timing_queue);
		return taken;
	}

	ShaderInfo *SessionModel::ShaderById(uint32_t id)
	{
		return (id != 0 && id <= m_shaders.size()) ? &m_shaders[id - 1] : nullptr;
	}

	PipelineInfo *SessionModel::PipelineById(uint32_t id)
	{
		return (id != 0 && id <= m_pipelines.size()) ? &m_pipelines[id - 1] : nullptr;
	}

	ResourceInfo *SessionModel::ResourceById(uint32_t id)
	{
		return (id != 0 && id <= m_resources.size()) ? &m_resources[id - 1] : nullptr;
	}

	const ShaderInfo *SessionModel::ShaderById(uint32_t id) const
	{
		return (id != 0 && id <= m_shaders.size()) ? &m_shaders[id - 1] : nullptr;
	}

	const PipelineInfo *SessionModel::PipelineById(uint32_t id) const
	{
		return (id != 0 && id <= m_pipelines.size()) ? &m_pipelines[id - 1] : nullptr;
	}

	const ResourceInfo *SessionModel::ResourceById(uint32_t id) const
	{
		return (id != 0 && id <= m_resources.size()) ? &m_resources[id - 1] : nullptr;
	}

	std::vector<uint32_t> SessionModel::EventsUsingShader(uint32_t shader_id) const
	{
		std::vector<uint32_t> result;
		if (shader_id == 0)
			return result;

		for (const FrameEvent &event : m_last_frame.events)
		{
			if (event.pipeline_id == 0 || event.pipeline_id > m_pipelines.size())
				continue;

			const PipelineInfo &pipeline = m_pipelines[event.pipeline_id - 1];
			for (uint32_t id : pipeline.shader_ids)
			{
				if (id == shader_id)
				{
					result.push_back(event.index);
					break;
				}
			}
		}
		return result;
	}

	std::vector<uint32_t> SessionModel::ShadersOfPipeline(uint32_t pipeline_id) const
	{
		if (const PipelineInfo *pipeline = PipelineById(pipeline_id))
			return pipeline->shader_ids;
		return {};
	}
}
