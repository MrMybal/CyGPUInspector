// CyGPUInspector — wire protocol shared by CyGPUInspectorRS (producer) and
// CyGPUInspectorApp (consumer).
//
// Two transports, one vocabulary:
//   * event ring buffer (shared memory, RS -> App): everything that happens per frame
//   * control pipe (named pipe, App -> RS + reply): commands and requests
//
// Everything here is plain old data with explicit sizes and no pointers, so that both sides can
// be built by different compilers and the records can be written straight to a capture file.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include <cstdint>

namespace cygi
{
	// Bumped whenever a record layout changes. The App refuses a session with a different value.
	inline constexpr uint32_t kProtocolVersion = 1;

	inline constexpr uint32_t kMaxProcessNameLength = 64;
	inline constexpr uint32_t kMaxObjectNameLength = 96;

	enum class GraphicsApi : uint32_t
	{
		unknown = 0,
		d3d9 = 0x9000,
		d3d10 = 0xa000,
		d3d11 = 0xb000,
		d3d12 = 0xc000,
		opengl = 0x10000,
		vulkan = 0x20000,
	};
	const char *GraphicsApiName(GraphicsApi api);

	// Mirrors reshade::api::shader_stage semantics but is independent from the SDK so that the
	// App and the database never have to include ReShade headers.
	enum class ShaderStage : uint32_t
	{
		unknown = 0,
		vertex,
		hull,
		domain,
		geometry,
		pixel,
		compute,
		amplification,
		mesh,
		raygen,
		any_hit,
		closest_hit,
		miss,
		intersection,
		callable,
	};
	const char *ShaderStageName(ShaderStage stage);

	// Format of the shader byte code, decides which disassembler / decompiler can handle it.
	enum class ShaderFormat : uint32_t
	{
		unknown = 0,
		dxbc,    // Shader Model 4.x / 5.x
		dxil,    // Shader Model 6.x (DXBC container with a DXIL part)
		spirv,
		glsl_source,
	};
	const char *ShaderFormatName(ShaderFormat format);

	// How much the add-on records. Higher levels cost more in the game process (see
	// Docs/CyGPUInspector_Architecture.md section 3.2).
	enum class TrackingLevel : uint32_t
	{
		idle = 0,
		tracking = 1,
		pass_timing = 2,
		capture = 3,
		full_draw_timing = 4,
	};
	const char *TrackingLevelName(TrackingLevel level);

	// CyGPUInspector has two captures, and they are two different tools rather than two settings
	// of one. Asking for "more detail" on the runtime one would eventually stall the game; asking
	// the deep one to run continuously would stall it immediately.
	//
	//   runtime  what the game can afford every frame, forever: the command stream, the pass
	//            boundaries and their GPU durations. This is what the frame timeline is drawn
	//            from, and it is meant to be left on while playing.
	//   deep     one shot, on a handful of frames, then off again: everything above plus every
	//            descriptor bound to every draw, the fixed function state of every pipeline, the
	//            barriers, and a timestamp per command rather than per pass.
	//
	// What neither can do is replay. A frame debugger like RenderDoc serialises the command
	// stream and re-executes it on its own device, which is what buys it pixel history and
	// per-draw stepping. CyGPUInspector observes through the official ReShade add-on API and
	// never injects its own device, by design (brief section 2), so it reports what happened
	// rather than re-running it. Docs/CaptureModes.md says exactly where the line falls.
	enum class CaptureMode : uint32_t
	{
		runtime = 0,
		deep = 1,
	};
	const char *CaptureModeName(CaptureMode mode);

	enum class ResourceKind : uint32_t
	{
		unknown = 0,
		buffer,
		texture_1d,
		texture_2d,
		texture_3d,
		surface,
	};
	const char *ResourceKindName(ResourceKind kind);

	// Bit flags describing what a resource may be used for (mirrors resource_usage).
	enum ResourceUsageFlags : uint32_t
	{
		kUsageNone = 0,
		kUsageRenderTarget = 1u << 0,
		kUsageDepthStencil = 1u << 1,
		kUsageShaderResource = 1u << 2,
		kUsageUnorderedAccess = 1u << 3,
		kUsageIndexBuffer = 1u << 4,
		kUsageVertexBuffer = 1u << 5,
		kUsageConstantBuffer = 1u << 6,
		kUsageIndirectArgument = 1u << 7,
		kUsageCopySource = 1u << 8,
		kUsageCopyDest = 1u << 9,
		kUsageResolveSource = 1u << 10,
		kUsageResolveDest = 1u << 11,
		kUsageBackBuffer = 1u << 12,
		kUsageShared = 1u << 13,
	};

	// ---------------------------------------------------------------------------------------
	// Event ring records
	// ---------------------------------------------------------------------------------------

	enum class RecordType : uint16_t
	{
		none = 0,
		padding,         // filler inserted so that a record never wraps around the ring
		session_info,    // SessionInfoRecord, sent on connect and on FullSync
		shader_code,     // ShaderCodeRecord + byte code
		pipeline_info,   // PipelineInfoRecord + uint32 shader ids
		resource_info,   // ResourceInfoRecord
		resource_named,  // ResourceNamedRecord + utf8 name
		resource_gone,   // ResourceGoneRecord
		frame_begin,     // FrameBeginRecord
		frame_events,    // FrameEventsRecord + FrameEvent[]
		frame_end,       // FrameEndRecord
		timing_results,  // TimingResultsRecord + TimingResult[]
		preview_ready,   // PreviewReadyRecord
		log_message,     // LogMessageRecord + utf8 text
		stats,           // StatsRecord (cost of the tool itself)
		draw_state,      // DrawStateRecord + DrawBinding[]      deep capture only
		barrier_set,     // BarrierSetRecord + BarrierEntry[]    deep capture only
		pipeline_state,  // PipelineStateRecord                  deep capture only
		capture_state,   // CaptureStateRecord, where a deep capture has got to
		frame_gpu_span,  // FrameGpuSpanRecord, where a measured frame sits on the GPU clock
		capture_buffer,  // CaptureBufferRecord, one texture of a captured frame, shared
		capture_scope,   // CaptureScopeRecord, the part of a captured frame the capture is about
	};

	// Every record in the ring starts with this header. `size` covers the header and the payload.
	struct RecordHeader
	{
		uint32_t size;
		RecordType type;
		uint16_t flags;
		uint64_t sequence;
	};
	static_assert(sizeof(RecordHeader) == 16, "RecordHeader must stay 16 bytes");

	struct SessionInfoRecord
	{
		uint32_t protocol_version;
		uint32_t process_id;
		GraphicsApi api;
		TrackingLevel level;
		uint32_t device_index;        // several devices may live in one process
		uint32_t capability_flags;    // see SessionCapabilityFlags
		uint64_t timestamp_frequency; // GPU ticks per second, 0 if timestamps are unavailable
		char process_name[kMaxProcessNameLength];
		char addon_version[16];
		char adapter_name[kMaxObjectNameLength];
	};

	enum SessionCapabilityFlags : uint32_t
	{
		kCapNone = 0,
		kCapSharedResource = 1u << 0,
		kCapSharedResourceNtHandle = 1u << 1,
		kCapSharedFence = 1u << 2,
		kCapTimestampQueries = 1u << 3,
		kCapDrawSkipping = 1u << 4,
		kCapShaderReplacement = 1u << 5,
		kCapDeepCapture = 1u << 6,       // descriptors, pipeline state and barriers can be read
		kCapPipelineState = 1u << 7,     // the graphics API reported fixed function state to us
	};

	struct ShaderCodeRecord
	{
		uint32_t shader_id;        // session local, stable, starts at 1
		ShaderStage stage;
		ShaderFormat format;
		uint32_t code_size;        // byte code follows the record
		uint8_t signature[32];     // SHA-256 of the normalized byte code
		uint8_t semantic_hash[32]; // SHA-256 of the code parts only
		uint32_t shader_model;     // 0xMN, e.g. 0x50 for SM 5.0, 0x66 for SM 6.6
		uint32_t reserved;
	};

	inline constexpr uint32_t kMaxPipelineShaders = 8;

	struct PipelineInfoRecord
	{
		uint32_t pipeline_id;    // session local
		uint64_t native_handle;  // reshade pipeline handle, for correlation only
		uint32_t stage_mask;     // bit per ShaderStage
		uint32_t shader_count;   // uint32 shader ids follow the record
		uint32_t layout_id;
		uint32_t is_compute;
	};

	struct ResourceInfoRecord
	{
		uint32_t resource_id;    // session local, stable, what the UI shows as "Resource #183"
		uint64_t native_handle;
		ResourceKind kind;
		uint32_t format;         // DXGI_FORMAT compatible value (reshade::api::format)
		uint32_t width;
		uint32_t height;
		uint32_t depth_or_layers;
		uint32_t mip_levels;
		uint32_t samples;
		uint32_t usage_flags;    // ResourceUsageFlags
		uint64_t buffer_size;    // buffers only
		uint64_t created_frame;
		uint32_t created_event;  // position inside that frame
		uint32_t reserved;
	};

	struct ResourceNamedRecord
	{
		uint32_t resource_id;
		uint32_t name_length;    // utf8 bytes follow the record
	};

	struct ResourceGoneRecord
	{
		uint32_t resource_id;
		uint32_t destroyed_event;
		uint64_t destroyed_frame;
	};

	struct FrameBeginRecord
	{
		uint64_t frame_index;
		uint64_t cpu_timestamp_qpc;
	};

	// What a single recorded command was.
	enum class EventKind : uint16_t
	{
		none = 0,
		draw,
		draw_indexed,
		draw_indirect,
		dispatch,
		dispatch_indirect,
		dispatch_mesh,
		dispatch_rays,
		copy_resource,
		copy_buffer_region,
		copy_texture_region,
		copy_buffer_to_texture,
		copy_texture_to_buffer,
		resolve,
		clear_render_target,
		clear_depth_stencil,
		clear_unordered_access,
		generate_mipmaps,
		barrier,
		begin_render_pass,
		end_render_pass,
		bind_render_targets,
		bind_pipeline,
		present,
	};
	const char *EventKindName(EventKind kind);

	// Whether a command is one that actually runs shaders. The draw and dispatch kinds are kept
	// adjacent in the enum above for exactly this, so adding one means adding it inside the run.
	inline constexpr bool IsDrawOrDispatch(EventKind kind)
	{
		return kind >= EventKind::draw && kind <= EventKind::dispatch_rays;
	}

	inline constexpr uint32_t kMaxTrackedRenderTargets = 8;

	// One command inside a frame. Fixed size so a frame is a flat array the App can binary search.
	struct FrameEvent
	{
		uint32_t index;          // position in the frame, 0 based
		EventKind kind;
		uint8_t queue_index;     // which command list recorded it
		uint8_t flags;           // EventFlags
		uint32_t pipeline_id;    // 0 when not applicable
		uint32_t primary_resource;   // render target 0, copy destination, cleared resource
		uint32_t secondary_resource; // depth target, copy source
		uint32_t a;              // vertex / index count, or group count X
		uint32_t b;              // instance count, or group count Y
		uint32_t c;              // first vertex / first index, or group count Z
		uint32_t d;              // vertex offset / first instance
		uint64_t gpu_ticks;      // 0 when the event was not timed
	};
	static_assert(sizeof(FrameEvent) == 48, "FrameEvent must stay compact");

	enum EventFlags : uint8_t
	{
		kEventNone = 0,
		kEventSkipped = 1u << 0,      // the add-on suppressed this command (shader disabled)
		kEventHighlighted = 1u << 1,
		kEventHasBindings = 1u << 2,  // a bindings record follows for this event
		kEventIndirect = 1u << 3,
		kEventReplaced = 1u << 4,      // ran through a replacement pipeline instead of the game's
	};

	struct FrameEventsRecord
	{
		uint64_t frame_index;
		uint32_t event_count;   // FrameEvent[] follows
		uint32_t first_index;   // index of the first event, for chunked frames
	};

	struct FrameEndRecord
	{
		uint64_t frame_index;
		uint32_t draw_count;
		uint32_t dispatch_count;
		uint32_t event_count;
		uint32_t new_shader_count;
		uint32_t new_resource_count;
		uint32_t dropped_events;   // ring was full: this many events were lost
		float cpu_frame_ms;        // wall clock between two presents
		float addon_cpu_ms;        // time spent inside our callbacks, the cost of the tool
	};

	// ---------------------------------------------------------------------------------------
	// Deep capture: what a single command was given to work with.
	//
	// None of this is recorded in runtime mode. Producing it means shadowing every binding call
	// the game makes and writing a record per draw, which is affordable for a handful of frames
	// and ruinous for all of them.
	// ---------------------------------------------------------------------------------------

	enum class SlotKind : uint8_t
	{
		constant_buffer = 0,
		shader_resource,
		unordered_access,
		sampler,
		vertex_buffer,
		index_buffer,
		render_target,
		depth_stencil,
	};
	const char *SlotKindName(SlotKind kind);

	// One resource, on one slot, for one draw.
	struct DrawBinding
	{
		uint32_t resource_id;    // 0 when the slot was bound to something we never saw created
		uint32_t offset;         // byte offset into a buffer
		uint32_t size;           // bytes for a buffer view, 0 for a texture
		uint16_t slot;           // register index: b3, t7, u1
		uint8_t kind;            // SlotKind
		uint8_t stage;           // ShaderStage, or unknown when the binding covers several
	};
	static_assert(sizeof(DrawBinding) == 16, "DrawBinding must stay compact");

	struct DrawStateRecord
	{
		uint64_t frame_index;
		uint32_t event_index;
		uint32_t pipeline_id;
		uint32_t binding_count;    // DrawBinding[] follows
		uint32_t topology;         // primitive topology, as reshade::api::primitive_topology
		float viewport[4];         // x, y, width, height of viewport 0
		float depth_range[2];
		int32_t scissor[4];        // left, top, right, bottom of scissor 0
		uint32_t viewport_count;
		uint32_t index_format;     // bytes per index: 2 or 4, 0 when no index buffer
		uint32_t stage_mask;       // reshade::api::pipeline_stage of the bound pipeline
		uint32_t flags;            // DrawStateFlags
	};

	enum DrawStateFlags : uint32_t
	{
		kDrawStateNone = 0,
		kDrawStateScissorValid = 1u << 0,
		kDrawStateViewportValid = 1u << 1,
		// Descriptor tables were bound rather than pushed, so some slots could not be read back.
		// Says so in the interface instead of showing an empty list as if nothing were bound.
		kDrawStateTablesUnresolved = 1u << 2,
	};

	// A resource transition. The frame graph derives dependencies from reads and writes, but the
	// barriers are what the game actually asked for, and the two disagreeing is worth seeing.
	struct BarrierEntry
	{
		uint32_t resource_id;
		uint32_t old_state;   // reshade::api::resource_usage
		uint32_t new_state;
		uint32_t reserved;
	};

	struct BarrierSetRecord
	{
		uint64_t frame_index;
		uint32_t event_index;
		uint32_t count;       // BarrierEntry[] follows
	};

	// The fixed function state of a pipeline, read once when it is created. It does not change
	// per draw, so it is sent once per pipeline rather than per command.
	struct PipelineStateRecord
	{
		uint32_t pipeline_id;
		uint32_t topology;
		uint32_t render_target_formats[kMaxTrackedRenderTargets];
		uint32_t depth_stencil_format;
		uint32_t depth_enable;
		uint32_t depth_write;
		uint32_t depth_func;
		uint32_t stencil_enable;
		uint32_t cull_mode;
		uint32_t fill_mode;
		uint32_t front_counter_clockwise;
		uint32_t blend_enable_mask;      // bit per render target
		uint32_t source_blend;
		uint32_t dest_blend;
		uint32_t blend_op;
		uint32_t render_target_write_mask;
		uint32_t sample_count;
		uint32_t sample_mask;
		uint32_t alpha_to_coverage;
		uint32_t known_fields;           // bit per field the API actually reported
	};

	// Where a deep capture has got to. The interface needs this to say "arming", "capturing
	// frame 2 of 3" or "done" rather than leaving the user watching nothing.
	enum class CaptureStage : uint32_t
	{
		idle = 0,
		armed,
		capturing,
		finished,
		failed,
	};
	const char *CaptureStageName(CaptureStage stage);

	struct CaptureStateRecord
	{
		CaptureStage stage;
		CaptureMode mode;
		uint64_t first_frame;
		uint32_t frames_requested;
		uint32_t frames_done;
		uint32_t draw_states_recorded;
		uint32_t bindings_recorded;
		uint32_t barriers_recorded;
		uint32_t dropped;         // records the ring could not take
	};

	struct TimingResult
	{
		uint32_t event_index;
		uint32_t pipeline_id;
		uint64_t gpu_ticks;      // until the next measured point on the GPU, in execution order
		// Where the measurement starts, in ticks since the earliest measured point of the frame.
		// It is what places a pass on a time axis rather than end to end, and it is only present
		// when the record says so (see result_size): an add-on built before it existed sends the
		// sixteen bytes above and nothing else.
		uint64_t gpu_start;
	};

	// Where a whole frame sits on the GPU clock, in absolute ticks of the queue's timestamp clock:
	// its earliest measured point and its closing timestamp, taken at present. Per frame starts in
	// TimingResult are relative to `gpu_begin`; this is what puts one frame after another on one
	// continuous axis, and what the gap between two frames is measured from. Sent just before the
	// timings of the same frame. A record type of its own, so an application that does not know it
	// skips it, and an add-on that does not send it leaves the continuous view saying so.
	struct FrameGpuSpanRecord
	{
		uint64_t frame_index;
		uint64_t gpu_begin;
		uint64_t gpu_end;
	};

	// The first layout of TimingResult, without gpu_start. Still accepted, because the add-on in
	// a game folder is not necessarily as new as the application reading it.
	inline constexpr uint32_t kTimingResultSizeV1 = 16;

	struct TimingResultsRecord
	{
		uint64_t frame_index;
		uint32_t count;          // TimingResult[] follows
		// Bytes per TimingResult that follows. Zero, which is what the field held when it was
		// still called `reserved`, means kTimingResultSizeV1.
		uint32_t result_size;
	};

	// Why a preview request did or did not produce a shared texture.
	enum class PreviewStatus : uint32_t
	{
		ready = 0,
		unknown_resource,
		not_a_texture,
		unsupported_format,
		multisampled,          // needs a resolve the add-on cannot do for this format
		sharing_unsupported,   // the device cannot create shared resources
		creation_failed,
		handle_duplication_failed,
		state_unknown,         // D3D12 / Vulkan: no barrier said which state it was in, not copied
		over_budget,           // a capture's buffers stop at a count and a size
	};
	const char *PreviewStatusName(PreviewStatus status);

	// A capture limited to a part of the frame: the 3D render of Unreal's main viewport, which the
	// plugin marks by clearing two textures of its own right before and right after it. Only the
	// commands from first_event to last_event were recorded; the rest of the frame (the editor's
	// own interface) is in the timeline but not in the capture.
	struct CaptureScopeRecord
	{
		uint64_t frame_index;
		uint32_t first_event;
		uint32_t last_event;
	};

	// What a captured buffer was to the frame, as bits: a texture can be several of these.
	enum CaptureBufferRole : uint32_t
	{
		kBufferRoleNone = 0,
		kBufferRoleRenderTarget = 1u << 0,
		kBufferRoleDepthStencil = 1u << 1,
		kBufferRoleUnorderedAccess = 1u << 2,
		kBufferRoleCopyDest = 1u << 3,
		kBufferRoleBackBuffer = 1u << 4,
	};

	// One texture of a deep capture's last frame, as it was at the end of that frame, copied
	// into a shared texture of its own. The standalone opens it, saves it and then sends
	// release_capture_buffers so the add-on can free them all. A capture sends `count` records,
	// failures included, so the standalone knows when it has them all.
	struct CaptureBufferRecord
	{
		uint64_t first_frame;      // the capture it belongs to, as in CaptureStateRecord
		uint64_t frame_index;      // the frame it was copied at the end of
		uint32_t resource_id;
		PreviewStatus status;
		uint64_t shared_handle;    // already duplicated into the App process for NT handles
		uint32_t width;
		uint32_t height;
		uint32_t format;           // of the shared copy
		uint32_t source_format;    // of the game's texture
		uint32_t is_nt_handle;
		uint32_t roles;            // CaptureBufferRole bits
		uint32_t first_event;      // the first command of the frame that used it
		uint32_t index;            // position in this capture's list, 0 based
		uint32_t count;            // records this capture sends
		uint32_t reserved;
	};

	// The add-on copied a resource into a shared texture; the App can open `shared_handle`.
	struct PreviewReadyRecord
	{
		uint32_t request_id;
		uint32_t resource_id;
		PreviewStatus status;
		uint32_t reserved;
		uint64_t shared_handle;   // HANDLE, already duplicated into the App process for NT handles
		uint64_t fence_handle;    // 0 when no shared fence is used
		uint64_t fence_value;
		uint32_t width;
		uint32_t height;
		uint32_t format;          // format of the shared texture, not of the source
		uint32_t is_nt_handle;
	};

	enum class LogLevel : uint32_t
	{
		info = 0,
		warning,
		error,
	};

	struct LogMessageRecord
	{
		LogLevel level;
		uint32_t text_length;   // utf8 text follows
	};

	struct StatsRecord
	{
		uint64_t frame_index;
		uint64_t bytes_written;
		uint64_t records_written;
		uint64_t dropped_bytes;
		uint32_t tracked_shaders;
		uint32_t tracked_resources;
		uint32_t tracked_pipelines;
		uint32_t shared_texture_bytes_kb;
	};

	// ---------------------------------------------------------------------------------------
	// Control channel (named pipe, message mode)
	// ---------------------------------------------------------------------------------------

	enum class ControlType : uint32_t
	{
		none = 0,
		hello,            // App -> RS, HelloRequest
		hello_ack,        // RS -> App, HelloAck
		ping,
		pong,
		set_level,        // SetLevelRequest
		capture_frame,    // CaptureFrameRequest
		full_sync,        // resend every shader / resource
		request_preview,  // PreviewRequest
		stop_preview,     // PreviewRequest (resource_id ignored)
		shader_command,   // ShaderCommandRequest (disable / enable / highlight / restore)
		replace_shader,   // ReplaceShaderRequest + byte code
		ack,              // ControlAck
		error,            // ControlAck with a message
		mcp_request,      // utf8 JSON, MCP proxy -> standalone
		mcp_response,     // utf8 JSON, standalone -> MCP proxy
		freeze_preview,   // FreezePreviewRequest: keep the shared image as it is, or refresh it again
		release_capture_buffers, // no payload: the standalone has what it needed of the capture's buffers
	};

	// What an MCP client is allowed to do. The standalone owns this, never the proxy.
	enum class McpPermission : uint32_t
	{
		read_only = 0,        // inspect, search, decompile, timings, frame graph
		debug_control,        // + disable, enable, highlight, capture
		shader_modification,  // + compile, replace, restore
	};
	const char *McpPermissionName(McpPermission permission);

	struct ControlHeader
	{
		uint32_t size;        // header + payload
		ControlType type;
		uint64_t request_id;
	};

	struct HelloRequest
	{
		uint32_t protocol_version;
		uint32_t app_process_id;   // needed to DuplicateHandle NT shared handles into the App
		char app_version[16];
	};

	struct HelloAck
	{
		uint32_t protocol_version;
		uint32_t accepted;
		uint32_t capability_flags;
		TrackingLevel level;
	};

	struct SetLevelRequest
	{
		TrackingLevel level;
	};

	// Arms a capture. In runtime mode this only says "hand me the frames you are already
	// recording"; in deep mode it switches the add-on into the expensive path for `frame_count`
	// frames and switches it back afterwards, whatever happens.
	struct CaptureFrameRequest
	{
		uint32_t frame_count;
		CaptureMode mode;
		uint32_t include_bindings;    // deep mode: record a descriptor list per draw
		uint32_t include_barriers;    // deep mode: record resource transitions
		uint32_t per_draw_timing;     // deep mode: a GPU timestamp per command, not per pass
		uint32_t max_draw_states;     // hard cap so one pathological frame cannot stall the game
		// deep mode: copy the textures the last captured frame rendered into, for the standalone
		// to save. Appended last: an add-on built before it reads a shorter request and never
		// sends buffers, and a request from an older standalone reads as 0 here.
		uint32_t include_buffers;
	};

	enum class PreviewChannels : uint32_t
	{
		rgb = 0,
		red,
		green,
		blue,
		alpha,
		depth_raw,
		depth_linear,
	};

	// A preview of this "resource" is whatever the swap chain presents: the game's final image of
	// the frame, before ReShade's own effects. Resolved by the add-on at present time, because the
	// back buffer being presented changes from one frame to the next.
	inline constexpr uint32_t kPreviewFinalImage = 0xFFFFFFFFu;

	// Stops refreshing the shared image without releasing it, so the image of the moment the
	// timeline was paused stays on screen instead of running on under a frozen timeline.
	struct FreezePreviewRequest
	{
		uint32_t frozen;
		uint32_t reserved;
	};

	struct PreviewRequest
	{
		uint32_t request_id;
		uint32_t resource_id;
		uint32_t mip_level;
		uint32_t array_slice;
		PreviewChannels channels;
		uint32_t max_width;      // 0 = native size
		uint32_t max_height;
	};

	enum class ShaderCommand : uint32_t
	{
		enable = 0,
		disable,
		highlight,
		unhighlight,
		restore,
	};

	struct ShaderCommandRequest
	{
		ShaderCommand command;
		uint32_t shader_id;
		uint8_t signature[32];   // used when shader_id is unknown to this session
	};

	struct ReplaceShaderRequest
	{
		uint32_t shader_id;
		uint32_t code_size;      // byte code follows
		uint8_t signature[32];
	};

	struct ControlAck
	{
		uint32_t succeeded;
		uint32_t message_length;  // utf8 text follows
	};

	// ---------------------------------------------------------------------------------------
	// Shared object naming
	// ---------------------------------------------------------------------------------------

	// Ring buffer of a session: Local\CyGPUInspectorRS.<pid>.<device>.events
	void MakeRingName(uint32_t process_id, uint32_t device_index, char *out, size_t out_size);
	// Data ready event: Local\CyGPUInspectorRS.<pid>.<device>.signal
	void MakeSignalName(uint32_t process_id, uint32_t device_index, char *out, size_t out_size);
	// Control pipe of a session: \\.\pipe\CyGPUInspector\<pid>.<device>
	void MakePipeName(uint32_t process_id, uint32_t device_index, char *out, size_t out_size);
	// Control pipe of the standalone, used by the MCP proxy: \\.\pipe\CyGPUInspector\app
	inline constexpr const char *kAppPipeName = "\\\\.\\pipe\\CyGPUInspector\\app";
}
