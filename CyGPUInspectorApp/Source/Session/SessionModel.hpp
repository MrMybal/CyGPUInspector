// CyGPUInspectorApp — in memory model of one connected session.
//
// The IPC thread applies records here; the UI thread reads under the same mutex. Everything is
// indexed by the session local ids assigned by the add-on, so lookups are array accesses.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/Sha256.hpp>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cygi
{
	struct ShaderInfo
	{
		uint32_t id = 0;
		Sha256Digest signature;
		Sha256Digest semantic_hash;
		ShaderStage stage = ShaderStage::unknown;
		ShaderFormat format = ShaderFormat::unknown;
		uint32_t shader_model = 0;
		uint32_t code_size = 0;
		std::vector<uint8_t> code;
		uint64_t first_seen_frame = 0;

		// Refreshed at the end of every frame.
		uint32_t draws_this_frame = 0;
		uint32_t dispatches_this_frame = 0;
		uint64_t total_draws = 0;
		uint64_t gpu_ticks = 0;          // summed over the timed frame, 0 when not profiling

		bool disabled = false;
		bool highlighted = false;
		bool replaced = false;

		std::string ShortSignature() const { return signature.ToShortHex(8); }
	};

	struct PipelineInfo
	{
		uint32_t id = 0;
		uint64_t native_handle = 0;
		std::vector<uint32_t> shader_ids;
		uint32_t stage_mask = 0;
		bool is_compute = false;
	};

	struct ResourceInfo
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

		uint32_t writes_this_frame = 0;
		uint32_t reads_this_frame = 0;
		uint64_t total_writes = 0;
		uint32_t first_write_event = 0;
		uint32_t last_write_event = 0;
		uint32_t last_read_event = 0;

		// The name the game gave it through the graphics API (SetName on Direct3D 12), as of the
		// last deep capture that used it. Empty when it has none: shipping builds seldom do.
		std::string name;

		std::string Describe() const;
	};

	// Everything a deep capture recorded about one command. Empty in runtime mode, which is the
	// point: this is what the expensive capture buys and the cheap one does not.
	//
	// Named CommandState rather than DrawState because <Windows.h> defines DrawState as a macro,
	// so any translation unit that included both would fail to compile — the same trap that named
	// the model's lookups ShaderById rather than FindShader.
	struct CommandState
	{
		DrawStateRecord record = {};
		std::vector<DrawBinding> bindings;

		bool TablesUnresolved() const { return (record.flags & kDrawStateTablesUnresolved) != 0; }
	};

	struct BarrierSet
	{
		uint32_t event_index = 0;
		std::vector<BarrierEntry> entries;
	};

	struct FrameInfo
	{
		uint64_t index = 0;
		std::vector<FrameEvent> events;
		uint32_t draw_count = 0;
		uint32_t dispatch_count = 0;
		uint32_t dropped_events = 0;
		float cpu_frame_ms = 0.0f;
		float addon_cpu_ms = 0.0f;

		// Deep capture payload, keyed by event index so a lookup stays a binary search.
		std::vector<CommandState> draw_states;
		std::vector<BarrierSet> barriers;

		const CommandState *DrawStateOfEvent(uint32_t event_index) const;
		const BarrierSet *BarriersOfEvent(uint32_t event_index) const;
		bool HasDeepCapture() const { return !draw_states.empty() || !barriers.empty(); }
	};

	// One entry per frame the standalone has seen, for the timeline's history strip. Small on
	// purpose: this is kept for every frame, so it has to stay a handful of numbers.
	struct FrameHistoryEntry
	{
		uint64_t index = 0;
		uint32_t draw_count = 0;
		uint32_t dispatch_count = 0;
		uint32_t event_count = 0;
		float cpu_frame_ms = 0.0f;
		float addon_cpu_ms = 0.0f;
		double gpu_ms = 0.0;      // filled in when the timings for that frame arrive
	};

	// GPU timings arrive a few frames after the events they measure, so they carry their own
	// frame index rather than pretending to belong to the frame on screen.
	struct FrameTimings
	{
		uint64_t frame_index = 0;
		uint64_t total_ticks = 0;
		uint64_t frequency = 0;
		std::unordered_map<uint32_t, uint64_t> by_event;
		// Where each measurement starts on the GPU, in ticks since the frame's earliest point.
		// Empty when the add-on predates it; the timeline then lays passes end to end and says so.
		std::unordered_map<uint32_t, uint64_t> start_by_event;
		bool HasStarts() const { return !start_by_event.empty(); }

		// Where the whole frame sits on the GPU clock, in absolute ticks: its earliest measured
		// point and its closing timestamp. Zero when the add-on does not send it.
		uint64_t gpu_begin = 0;
		uint64_t gpu_end = 0;
		bool HasSpan() const { return gpu_end > gpu_begin; }

		double Milliseconds(uint64_t ticks) const
		{
			return frequency != 0 ? (static_cast<double>(ticks) * 1000.0) / static_cast<double>(frequency) : 0.0;
		}
		bool IsValid() const { return !by_event.empty() && frequency != 0; }
	};

	// Enough to cover several seconds of play at any frame rate, and small enough that keeping
	// it costs nothing: one entry is forty bytes.
	inline constexpr size_t kMaxFrameHistory = 600;

	// Frames kept whole after they stop being the last one. Timings arrive three or four frames
	// after the frame they measure, and the continuous timeline needs that frame's own events to
	// cut it into passes, not the events of whatever frame is on screen by then.
	inline constexpr size_t kRecentFrames = 8;

	// Timings waiting for the interface to take them. The game can run faster than the interface
	// draws, and the last timings alone would leave holes in a continuous timeline.
	inline constexpr size_t kMaxQueuedTimings = 64;

	class SessionModel
	{
	public:
		// Called from the IPC thread for every record read from the ring.
		void ApplyRecord(const RecordHeader &header, const uint8_t *payload, uint32_t payload_size);
		void Clear();

		// The UI locks around its reads; everything below assumes the lock is held.
		std::mutex &Mutex() const { return m_mutex; }

		const SessionInfoRecord &Session() const { return m_session; }
		bool HasSession() const { return m_has_session; }

		const std::vector<ShaderInfo> &Shaders() const { return m_shaders; }
		const std::vector<PipelineInfo> &Pipelines() const { return m_pipelines; }
		const std::vector<ResourceInfo> &Resources() const { return m_resources; }
		const FrameInfo &LastFrame() const { return m_last_frame; }
		// The frame with that index if it is still held: the last one or one of the few before it.
		const FrameInfo *FrameByIndex(uint64_t index) const;
		// Every set of timings received since the last call, oldest first. When the interface
		// falls more than kMaxQueuedTimings behind, the oldest are dropped and counted.
		std::deque<FrameTimings> TakeQueuedTimings();
		uint64_t DroppedTimings() const { return m_dropped_timings; }
		const std::vector<FrameHistoryEntry> &History() const { return m_history; }

		// The last frame a deep capture recorded state for, kept apart from the live frame. A
		// deep capture is a one shot: without this the frame the user asked the game for would
		// be gone from the interface a sixtieth of a second after it arrived.
		const FrameInfo &DeepFrame() const { return m_deep_frame; }
		bool HasDeepFrame() const { return m_deep_frame.HasDeepCapture(); }

		// The last thing the add-on said about a deep capture, and whether one is running.
		const CaptureStateRecord &CaptureState() const { return m_capture_state; }
		bool DeepCaptureRunning() const
		{
			return m_capture_state.stage == CaptureStage::armed ||
			       m_capture_state.stage == CaptureStage::capturing;
		}
		// The fixed function state of a pipeline, when a deep capture reported it.
		const PipelineStateRecord *PipelineStateById(uint32_t pipeline_id) const;
		const StatsRecord &Stats() const { return m_stats; }
		const FrameTimings &Timings() const { return m_timings; }

		// The last preview announcement, and whether the UI still has to act on it.
		const PreviewReadyRecord &Preview() const { return m_preview; }
		bool TakePreviewUpdate(PreviewReadyRecord &out);

		// The buffers of a deep capture, as the add-on shares them: taken once, by whoever saves
		// them. Each carries a share handle that is the taker's to close.
		bool TakeCaptureBuffers(std::vector<CaptureBufferRecord> &out);

		// The part of the last captured frame the capture was limited to (the 3D render of an
		// Unreal viewport), or frame_index 0 when it covered the whole frame.
		const CaptureScopeRecord &CaptureScope() const { return m_capture_scope; }

		// Named *ById rather than Find*: <Windows.h> defines FindResource as a macro, and any
		// translation unit that includes it would otherwise fail to compile.
		ShaderInfo *ShaderById(uint32_t id);
		PipelineInfo *PipelineById(uint32_t id);
		ResourceInfo *ResourceById(uint32_t id);
		const ShaderInfo *ShaderById(uint32_t id) const;
		const PipelineInfo *PipelineById(uint32_t id) const;
		const ResourceInfo *ResourceById(uint32_t id) const;

		// Draw / dispatch events of the last frame that ran a given shader.
		std::vector<uint32_t> EventsUsingShader(uint32_t shader_id) const;
		// Shader ids of a pipeline, empty when the pipeline is unknown.
		std::vector<uint32_t> ShadersOfPipeline(uint32_t pipeline_id) const;

		uint64_t RecordsApplied() const { return m_records_applied; }
		uint64_t BytesApplied() const { return m_bytes_applied; }
		const std::vector<std::string> &Log() const { return m_log; }

	private:
		void BeginFrame(const FrameBeginRecord &record);
		void AppendEvents(const FrameEventsRecord &record, const FrameEvent *events);
		void EndFrame(const FrameEndRecord &record);
		void RecomputeFrameStatistics();
		void ApplyTimings(const TimingResultsRecord &record, const TimingResult *results, bool with_starts);
		void ApplyDrawState(const DrawStateRecord &record, const DrawBinding *bindings);
		void ApplyBarriers(const BarrierSetRecord &record, const BarrierEntry *entries);
		FrameInfo *FrameForDeepRecord(uint64_t frame_index);

		mutable std::mutex m_mutex;

		SessionInfoRecord m_session = {};
		bool m_has_session = false;

		std::vector<ShaderInfo> m_shaders;       // indexed by id - 1
		std::vector<PipelineInfo> m_pipelines;   // indexed by id - 1
		std::vector<ResourceInfo> m_resources;   // indexed by id - 1

		FrameInfo m_building;                    // frame currently being received
		FrameInfo m_last_frame;                  // last completed frame, what the UI shows
		std::deque<FrameInfo> m_recent_frames;   // the few before it, oldest first
		FrameGpuSpanRecord m_pending_span = {};  // arrives just before the timings it belongs to
		std::deque<FrameTimings> m_timing_queue;
		uint64_t m_dropped_timings = 0;
		FrameInfo m_deep_frame;                  // last frame a deep capture recorded state for
		StatsRecord m_stats = {};
		FrameTimings m_timings;
		std::vector<FrameHistoryEntry> m_history;
		CaptureStateRecord m_capture_state = {};
		std::unordered_map<uint32_t, PipelineStateRecord> m_pipeline_states;
		PreviewReadyRecord m_preview = {};
		bool m_preview_pending = false;
		std::vector<CaptureBufferRecord> m_capture_buffers;
		CaptureScopeRecord m_capture_scope = {};

		uint64_t m_records_applied = 0;
		uint64_t m_bytes_applied = 0;
		std::vector<std::string> m_log;
	};
}
