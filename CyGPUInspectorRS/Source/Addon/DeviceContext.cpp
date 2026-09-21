// CyGPUInspectorRS — per device state and ReShade event handlers.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "DeviceContext.hpp"

#include "Config.hpp"
#include "InProcess.hpp"
#include "Log.hpp"
#include "Tracking/ResourceNames.hpp"

#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>

using namespace reshade::api;

namespace cygi
{
	namespace
	{
		std::atomic<uint32_t> g_next_device_index{ 0 };
		std::atomic<uint32_t> g_next_queue_index{ 0 };

		double QpcToMilliseconds(uint64_t ticks)
		{
			static LARGE_INTEGER frequency = []() {
				LARGE_INTEGER value = {};
				QueryPerformanceFrequency(&value);
				return value;
			}();
			return frequency.QuadPart != 0 ? (static_cast<double>(ticks) * 1000.0) / static_cast<double>(frequency.QuadPart) : 0.0;
		}

		uint64_t Qpc()
		{
			LARGE_INTEGER counter = {};
			QueryPerformanceCounter(&counter);
			return static_cast<uint64_t>(counter.QuadPart);
		}

		std::string ProcessName()
		{
			wchar_t path[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, path, MAX_PATH);
			const std::filesystem::path exe(path);
			return exe.filename().string();
		}

		ShaderStage StageOfSubobject(pipeline_subobject_type type)
		{
			switch (type)
			{
			case pipeline_subobject_type::vertex_shader: return ShaderStage::vertex;
			case pipeline_subobject_type::hull_shader: return ShaderStage::hull;
			case pipeline_subobject_type::domain_shader: return ShaderStage::domain;
			case pipeline_subobject_type::geometry_shader: return ShaderStage::geometry;
			case pipeline_subobject_type::pixel_shader: return ShaderStage::pixel;
			case pipeline_subobject_type::compute_shader: return ShaderStage::compute;
			case pipeline_subobject_type::amplification_shader: return ShaderStage::amplification;
			case pipeline_subobject_type::mesh_shader: return ShaderStage::mesh;
			case pipeline_subobject_type::raygen_shader: return ShaderStage::raygen;
			case pipeline_subobject_type::any_hit_shader: return ShaderStage::any_hit;
			case pipeline_subobject_type::closest_hit_shader: return ShaderStage::closest_hit;
			case pipeline_subobject_type::miss_shader: return ShaderStage::miss;
			case pipeline_subobject_type::intersection_shader: return ShaderStage::intersection;
			case pipeline_subobject_type::callable_shader: return ShaderStage::callable;
			default: return ShaderStage::unknown;
			}
		}

		bool IsShaderSubobject(pipeline_subobject_type type)
		{
			return StageOfSubobject(type) != ShaderStage::unknown;
		}

		CommandListState *StateOf(command_list *command_list)
		{
			return command_list != nullptr ? command_list->get_private_data<CommandListState>() : nullptr;
		}

		// Set while the add-on issues its own commands. ReShade dispatches its events from the
		// hooked API entry points, so a draw we issue ourselves would come straight back here.
		thread_local bool g_inside_replacement = false;

		struct ReplacementScope
		{
			ReplacementScope() { g_inside_replacement = true; }
			~ReplacementScope() { g_inside_replacement = false; }
		};

		// Records a timestamp for this command when the current level asks for it. The event index
		// is still local to the command list here and is fixed up when the list is merged.
		void MarkTiming(DeviceContext &context, command_list *cmd, CommandListState &state,
		                const FrameEvent &event);

		// Issues the draw through the replacement pipeline and puts the original back. Returns
		// true when it did, which tells the callback to suppress the game's own command.
		template <typename Issue>
		bool IssueThroughReplacement(command_list *cmd, CommandListState &state, bool compute,
		                             uint64_t replacement, const Issue &issue)
		{
			const uint64_t original = compute ? state.compute_pipeline_native : state.graphics_pipeline_native;
			const uint32_t stages = compute ? state.compute_pipeline_stages : state.graphics_pipeline_stages;
			if (replacement == 0 || original == 0 || stages == 0)
				return false;

			ReplacementScope scope;
			cmd->bind_pipeline(static_cast<pipeline_stage>(stages), pipeline{ replacement });
			issue();
			cmd->bind_pipeline(static_cast<pipeline_stage>(stages), pipeline{ original });
			return true;
		}

		// A draw/dispatch was recorded: fill the fields that come from the current bindings.
		void FillFromBindings(FrameEvent &event, const CommandListState &state, bool compute)
		{
			event.pipeline_id = compute ? state.compute_pipeline_id : state.graphics_pipeline_id;
			event.primary_resource = compute ? 0 : state.PrimaryRenderTarget();
			event.secondary_resource = compute ? 0 : state.depth_target_id;
		}

		// One branch on the hot path of every draw. In runtime mode it is a relaxed atomic load
		// that says no, and nothing else happens.
		void CaptureStateOfLastCommand(DeviceContext &context, CommandListState &state, bool compute)
		{
			if (!context.capture.Recording())
				return;
			RecordDrawState(state, context.capture, compute,
				compute ? state.compute_pipeline_stages : state.graphics_pipeline_stages);
		}
	}

	DeviceContext::DeviceContext(reshade::api::device *device_)
		: device(device_)
	{
		api = static_cast<GraphicsApi>(device_->get_api());
		device_index = g_next_device_index.fetch_add(1);

		if (device_->check_capability(device_caps::shared_resource))
			capability_flags |= kCapSharedResource;
		if (device_->check_capability(device_caps::shared_resource_nt_handle))
			capability_flags |= kCapSharedResourceNtHandle;
		if (device_->check_capability(device_caps::shared_fence))
			capability_flags |= kCapSharedFence;
		capability_flags |= kCapDrawSkipping;
		// The deep capture needs no device capability: it reads what the add-on API reports. What
		// it cannot read, it says so about, rather than pretending.
		capability_flags |= kCapDeepCapture;

		// Timestamp queries are not advertised by device_caps: probe them once.
		query_heap probe = {};
		if (device_->create_query_heap(query_type::timestamp, 2, &probe))
		{
			capability_flags |= kCapTimestampQueries;
			device_->destroy_query_heap(probe);

			// Sized for a heavy frame in full draw timing; smaller levels use a fraction of it.
			timer.Initialize(device_, static_cast<uint32_t>(config::GetInt("MaxTimestamps", 16384)));
		}
		capability_flags |= kCapShaderReplacement;

		char description[256] = {};
		device_->get_property(device_properties::description, description);

		const std::string process_name = ProcessName();
		if (!ipc.Start(device_index, api, process_name.c_str(), capability_flags, timestamp_frequency, description))
			log::Error("IPC could not be started, the standalone will not see this device");

		frames.SetMaxEvents(static_cast<uint32_t>(config::GetInt("MaxEventsPerFrame", 250000)));

		log::Info("Device %u initialized (%s, adapter '%s', caps 0x%X)", device_index,
			GraphicsApiName(api), description, capability_flags);
	}

	DeviceContext::~DeviceContext()
	{
		control.ClearAllReplacements(device);
		timer.Shutdown(device);
		preview.Shutdown(device);
		capture_buffers.Shutdown(device);
		ipc.Stop();
	}

	void DeviceContext::RegisterImmediateState(CommandListState *state)
	{
		std::lock_guard<std::mutex> lock(immediate_mutex);
		if (std::find(immediate_states.begin(), immediate_states.end(), state) == immediate_states.end())
			immediate_states.push_back(state);
	}

	void DeviceContext::UnregisterImmediateState(CommandListState *state)
	{
		std::lock_guard<std::mutex> lock(immediate_mutex);
		immediate_states.erase(std::remove(immediate_states.begin(), immediate_states.end(), state),
			immediate_states.end());
	}

	bool DeviceContext::IsPrimaryPresent(swapchain *swapchain)
	{
		if (swapchain == nullptr)
			return true;

		const resource back_buffer = swapchain->get_current_back_buffer();
		const resource_desc desc = device->get_resource_desc(back_buffer);
		const uint64_t area = static_cast<uint64_t>(desc.texture.width) * desc.texture.height;
		const uint64_t id = reinterpret_cast<uint64_t>(swapchain);

		// A primary that has not presented while the others presented this many times is gone
		// (closed, minimised); the next one to present takes over.
		constexpr uint32_t kPrimaryAbsentPresents = 240;

		std::lock_guard<std::mutex> lock(present_mutex);
		if (primary_swapchain == 0 || id == primary_swapchain)
		{
			if (primary_swapchain != id)
				log::Info("Frames are counted on the swap chain presenting %ux%u", desc.texture.width, desc.texture.height);
			primary_swapchain = id;
			primary_area = area;
			presents_without_primary = 0;
			return true;
		}
		// A larger window takes over: the main window of a program that showed a splash first.
		if (area > primary_area || ++presents_without_primary > kPrimaryAbsentPresents)
		{
			log::Info("Frames are now counted on the swap chain presenting %ux%u", desc.texture.width, desc.texture.height);
			primary_swapchain = id;
			primary_area = area;
			presents_without_primary = 0;
			return true;
		}
		return false;
	}

	bool DeviceContext::ArmDeepCapture(const CaptureFrameRequest &request, const char *&refusal)
	{
		const TrackingLevel current = level.load(std::memory_order_relaxed);
		if (current == TrackingLevel::idle)
		{
			refusal = "tracking is idle: there would be no frame to capture";
			return false;
		}

		// Whatever the previous capture's buffers still hold goes back to the game.
		capture_buffers.BeginCapture(frames.FrameIndex());
		capture.Arm(request, current);
		// A deep capture times every command, so the level goes up for its duration and is put
		// back by EndFrame whatever happens.
		if (request.per_draw_timing != 0)
			level.store(TrackingLevel::full_draw_timing, std::memory_order_relaxed);
		ipc.PublishCaptureState(capture.State());
		log::Info("Deep capture armed for %u frame(s)", request.frame_count);
		return true;
	}

	void DeviceContext::HandleControlMessages()
	{
		ControlMessage message;
		while (ipc.PopControlMessage(message))
		{
			switch (message.type)
			{
			case ControlType::hello:
			{
				const HelloRequest *request = message.As<HelloRequest>();
				HelloAck ack = {};
				ack.protocol_version = kProtocolVersion;
				ack.capability_flags = capability_flags;
				ack.level = level.load(std::memory_order_relaxed);
				ack.accepted = (request != nullptr && request->protocol_version == kProtocolVersion) ? 1u : 0u;
				if (request != nullptr)
					ipc.SetAppProcessId(request->app_process_id);

				ipc.Pipe().Send(ControlType::hello_ack, message.request_id, &ack, sizeof(ack));

				if (ack.accepted != 0)
				{
					log::Info("Standalone connected (PID %u), sending a full sync",
						request != nullptr ? request->app_process_id : 0);
					ipc.PublishSessionInfo(level.load(std::memory_order_relaxed));
					shaders.QueueEverything();
					resources.QueueEverything();
				}
				else
				{
					log::Warning("Standalone refused: protocol version mismatch");
				}
				break;
			}
			case ControlType::ping:
				ipc.Pipe().Send(ControlType::pong, message.request_id, nullptr, 0);
				break;
			case ControlType::full_sync:
				shaders.QueueEverything();
				resources.QueueEverything();
				ipc.PublishSessionInfo(level.load(std::memory_order_relaxed));
				ipc.Pipe().SendAck(message.request_id, true);
				break;
			case ControlType::set_level:
			{
				const SetLevelRequest *request = message.As<SetLevelRequest>();
				if (request != nullptr)
				{
					level.store(request->level, std::memory_order_relaxed);
					ipc.PublishSessionInfo(request->level);
					log::Info("Tracking level set to %s", TrackingLevelName(request->level));
				}
				ipc.Pipe().SendAck(message.request_id, request != nullptr);
				break;
			}
			case ControlType::capture_frame:
			{
				// A standalone built before `include_buffers` sends a shorter request: what it does
				// not say reads as 0.
				if (message.payload.size() < offsetof(CaptureFrameRequest, include_buffers))
				{
					ipc.Pipe().SendAck(message.request_id, false, "malformed request");
					break;
				}
				CaptureFrameRequest copy = {};
				std::memcpy(&copy, message.payload.data(), std::min(message.payload.size(), sizeof(copy)));
				const CaptureFrameRequest *request = &copy;

				if (request->mode != CaptureMode::deep)
				{
					// A runtime capture asks for nothing from the game: the standalone already has
					// the frames, and saving one is its own business.
					ipc.Pipe().SendAck(message.request_id, true, "runtime capture needs no arming");
					break;
				}

				const char *refusal = nullptr;
				const bool armed = ArmDeepCapture(*request, refusal);
				ipc.Pipe().SendAck(message.request_id, armed, armed ? nullptr : refusal);
				break;
			}
			case ControlType::request_preview:
			{
				const PreviewRequest *request = message.As<PreviewRequest>();
				if (request == nullptr)
				{
					ipc.Pipe().SendAck(message.request_id, false, "malformed request");
					break;
				}
				preview.SetRequest(*request);
				ipc.Pipe().SendAck(message.request_id, true);
				break;
			}
			case ControlType::stop_preview:
				preview.ClearRequest();
				ipc.Pipe().SendAck(message.request_id, true);
				break;
			case ControlType::release_capture_buffers:
				capture_buffers.Release(frames.FrameIndex());
				ipc.Pipe().SendAck(message.request_id, true);
				break;
			case ControlType::freeze_preview:
			{
				const FreezePreviewRequest *request = message.As<FreezePreviewRequest>();
				if (request == nullptr)
				{
					ipc.Pipe().SendAck(message.request_id, false, "malformed request");
					break;
				}
				preview.SetFrozen(request->frozen != 0);
				ipc.Pipe().SendAck(message.request_id, true);
				break;
			}
			case ControlType::shader_command:
			{
				const ShaderCommandRequest *request = message.As<ShaderCommandRequest>();
				if (request == nullptr)
				{
					ipc.Pipe().SendAck(message.request_id, false, "malformed request");
					break;
				}

				uint32_t shader_id = request->shader_id;
				if (shader_id == 0)
				{
					Sha256Digest signature;
					std::memcpy(signature.bytes.data(), request->signature, signature.bytes.size());
					shader_id = shaders.ShaderIdBySignature(signature);
				}
				if (shader_id == 0)
				{
					ipc.Pipe().SendAck(message.request_id, false, "unknown shader");
					break;
				}

				switch (request->command)
				{
				case ShaderCommand::disable: control.SetShaderDisabled(shader_id, true, shaders); break;
				case ShaderCommand::enable: control.SetShaderDisabled(shader_id, false, shaders); break;
				case ShaderCommand::highlight: control.SetShaderHighlighted(shader_id, true, shaders); break;
				case ShaderCommand::unhighlight: control.SetShaderHighlighted(shader_id, false, shaders); break;
				case ShaderCommand::restore:
					control.RestoreShader(shader_id, shaders);
					control.ClearReplacement(device, shader_id);
					break;
				}
				ipc.Pipe().SendAck(message.request_id, true);
				break;
			}
			case ControlType::replace_shader:
			{
				const ReplaceShaderRequest *request = message.As<ReplaceShaderRequest>();
				const uint8_t *code = message.Blob(sizeof(ReplaceShaderRequest));
				const size_t code_size = message.BlobSize(sizeof(ReplaceShaderRequest));

				if (request == nullptr || code == nullptr || code_size == 0 ||
				    code_size < request->code_size)
				{
					ipc.Pipe().SendAck(message.request_id, false, "malformed replacement");
					break;
				}

				uint32_t shader_id = request->shader_id;
				if (shader_id == 0)
				{
					Sha256Digest signature;
					std::memcpy(signature.bytes.data(), request->signature, signature.bytes.size());
					shader_id = shaders.ShaderIdBySignature(signature);
				}
				if (shader_id == 0)
				{
					ipc.Pipe().SendAck(message.request_id, false, "unknown shader");
					break;
				}

				std::string error;
				const uint32_t swapped = control.SetReplacement(device, shaders, shader_id, code,
					request->code_size, error);
				if (swapped != 0)
				{
					log::Info("Shader %u replaced in %u pipeline(s)%s%s", shader_id, swapped,
						error.empty() ? "" : " - ", error.c_str());
					ipc.Pipe().SendAck(message.request_id, true, error.empty() ? nullptr : error.c_str());
				}
				else
				{
					log::Warning("Shader %u could not be replaced: %s", shader_id, error.c_str());
					ipc.Pipe().SendAck(message.request_id, false, error.c_str());
				}
				break;
			}
			default:
				ipc.Pipe().SendAck(message.request_id, false, "not implemented yet");
				break;
			}
		}
	}

	// A list's events join the frame, and the timestamps taken on it learn which commands of the
	// frame they measure. Always the two together: merging without attributing is how every
	// measurement of a D3D12 frame ended up on its first few commands.
	void DeviceContext::MergeCommandList(CommandListState &state)
	{
		const FrameRecorder::MergeResult merged = frames.Merge(state);
		timer.Attribute(state.timing_marks, merged.base, merged.accepted);
		state.timing_marks.clear();
	}

	void DeviceContext::EndFrame(reshade::api::command_queue *queue, reshade::api::swapchain *swapchain)
	{
		const uint64_t start_qpc = Qpc();
		const TrackingLevel current_level = level.load(std::memory_order_relaxed);

		// 1. Merge whatever the immediate context recorded (D3D11 never executes a command list).
		{
			std::lock_guard<std::mutex> lock(immediate_mutex);
			for (CommandListState *state : immediate_states)
				MergeCommandList(*state);
		}

		// 2. Commands coming from the standalone run here, on the thread that owns the device, and
		// so does a capture asked for from inside the process (the Unreal plugin).
		HandleControlMessages();
		{
			CaptureFrameRequest request = {};
			if (inprocess::TakeCaptureRequest(request))
			{
				const char *refusal = nullptr;
				if (!ArmDeepCapture(request, refusal))
					log::Warning("In-process capture request refused: %s", refusal);
			}
		}

		const uint64_t frame_index = frames.FrameIndex();

		// What the host says the frame is about, if anything: read once per frame.
		viewport_scope = inprocess::GetViewportScope();

		// 3. Publish everything discovered since the last frame.
		const std::vector<PendingShaderCode> new_shaders = shaders.TakePendingShaders();
		for (const PendingShaderCode &pending : new_shaders)
		{
			ShaderRecord record;
			if (shaders.CopyShader(pending.shader_id, record))
				ipc.PublishShader(record, pending.code.data(), static_cast<uint32_t>(pending.code.size()));
		}

		const std::vector<PipelineRecord> new_pipelines = shaders.TakePendingPipelines();
		for (const PipelineRecord &record : new_pipelines)
			ipc.PublishPipeline(record);

		const std::vector<ResourceRecord> new_resources = resources.TakePendingResources();
		for (const ResourceRecord &record : new_resources)
			ipc.PublishResource(record);

		for (const ResourceRecord &gone : resources.TakeDestroyedResources())
			ipc.PublishResourceGone(gone.id, gone.destroyed_frame, gone.destroyed_event);

		// 4. The frame itself.
		const uint32_t draw_count = frames.DrawCount();
		const uint32_t dispatch_count = frames.DispatchCount();
		const uint32_t dropped_events = frames.DroppedEvents();
		std::vector<FrameEvent> events = frames.TakeFrame();

		if (current_level != TrackingLevel::idle)
		{
			ipc.PublishFrameBegin(frame_index);
			ipc.PublishFrameEvents(frame_index, events);

			FrameEndRecord end = {};
			end.frame_index = frame_index;
			end.draw_count = draw_count;
			end.dispatch_count = dispatch_count;
			end.event_count = static_cast<uint32_t>(events.size());
			end.new_shader_count = static_cast<uint32_t>(new_shaders.size());
			end.new_resource_count = static_cast<uint32_t>(new_resources.size());
			end.dropped_events = dropped_events;
			end.cpu_frame_ms = last_present_qpc != 0
				? static_cast<float>(QpcToMilliseconds(start_qpc - last_present_qpc)) : 0.0f;
			end.addon_cpu_ms = static_cast<float>(addon_cpu_ms);
			ipc.PublishFrameEnd(end);
		}

		// 4b. Deep capture: the per command state recorded alongside those events. It is sent
		// after the events themselves so that the standalone already knows the timeline the
		// event indices refer to.
		if (capture.Recording() && !capture.BeginFrame(frame_index))
		{
			// Armed during this present: nothing of this frame was recorded; what the callbacks
			// may have gathered since the arming belongs to no captured frame.
			frames.TakeCapturedState();
			ipc.PublishCaptureState(capture.State());
		}
		else if (capture.Recording())
		{
			const FrameRecorder::CapturedState captured = frames.TakeCapturedState();
			// Every frame's transitions count for the state each texture ends in, kept or not.
			capture_buffers.NoteStates(captured);

			// When the host said what the capture is about — the Unreal plugin marks the 3D render
			// of the main viewport — only that part of the frame is kept, and a frame without it
			// (a viewport that did not redraw) is skipped rather than captured empty.
			uint32_t first_event = 0;
			uint32_t last_event = UINT32_MAX;
			const bool scoped = viewport_scope.HasMarkers();
			const bool in_scope = !scoped || FindViewportScope(events, first_event, last_event);

			if (!in_scope)
			{
				TrackingLevel restore = TrackingLevel::tracking;
				if (!capture.SkipFrame(frame_index, restore))
				{
					level.store(restore, std::memory_order_relaxed);
					ipc.PublishSessionInfo(restore);
					log::Warning("Deep capture aborted: %s", capture.FailureReason());
				}
				ipc.PublishCaptureState(capture.State());
			}
			else
			{
				PublishPipelineStates();
				if (scoped)
				{
					CaptureScopeRecord scope_record = {};
					scope_record.frame_index = frame_index;
					scope_record.first_event = first_event;
					scope_record.last_event = last_event;
					ipc.PublishCaptureScope(scope_record);
				}

				for (const CommandListState::RecordedDrawState &draw : captured.draw_states)
				{
					if (draw.record.event_index < first_event || draw.record.event_index > last_event)
						continue;
					DrawStateRecord record = draw.record;
					record.frame_index = frame_index;
					ipc.PublishDrawState(record, captured.bindings.data() + draw.binding_offset);
				}
				for (const CommandListState::RecordedBarrierSet &barriers : captured.barrier_sets)
				{
					if (barriers.record.event_index < first_event || barriers.record.event_index > last_event)
						continue;
					BarrierSetRecord record = barriers.record;
					record.frame_index = frame_index;
					ipc.PublishBarriers(record, captured.barrier_entries.data() + barriers.entry_offset);
				}

				// Names before buffers: the standalone names the files after them.
				PublishResourceNames(events, captured);

				const bool wants_buffers = capture.WantsBuffers();
				if (wants_buffers)
					capture_buffers.NoteFrame(events, captured, first_event, last_event);

				TrackingLevel restore = TrackingLevel::tracking;
				if (capture.EndFrame(restore))
				{
					// The textures of the last captured frame, copied as they are at its end, before
					// the capture says it has finished: the standalone then has them all when it sees it.
					if (wants_buffers)
						for (const CaptureBufferRecord &record : capture_buffers.Snapshot(device, queue, resources,
						         capture.State().first_frame, frame_index, ipc.AppProcessId()))
							ipc.PublishCaptureBuffer(record);

					level.store(restore, std::memory_order_relaxed);
					ipc.PublishSessionInfo(restore);
					log::Info("Deep capture finished: %u draw states, %u bindings, %u barriers",
						capture.State().draw_states_recorded, capture.State().bindings_recorded,
						capture.State().barriers_recorded);
					// The image of the frame that was just captured, kept for the standalone to show
					// and save: the preview copies it below, in this same present, and then holds it.
					preview.KeepThisFrame();
				}
				ipc.PublishCaptureState(capture.State());
			}
		}

		// 5. GPU timings: close the frame, then publish whatever finished three frames ago.
		if (queue != nullptr)
		{
			timer.EndFrame(device, queue, frame_index);

			uint64_t timed_frame = 0;
			uint64_t gpu_begin = 0;
			uint64_t gpu_end = 0;
			std::vector<TimingResult> timings;
			if (timer.TakeResults(timed_frame, timings, gpu_begin, gpu_end) && !timings.empty())
			{
				// The span first: the application attaches it to the timings that follow.
				ipc.PublishFrameGpuSpan(timed_frame, gpu_begin, gpu_end);
				ipc.PublishTimings(timed_frame, timings);
			}

			// Timing is the part with the most ways to silently produce nothing: a driver that
			// refuses the query heap, marks that are never taken because no pass boundary is
			// recognised, results that never resolve. Once a second, while a timing level is
			// selected, say which of those it is rather than leaving an empty column.
			const TrackingLevel active = level.load(std::memory_order_relaxed);
			if ((frame_index % 120) == 0 && (active == TrackingLevel::pass_timing ||
			                                 active == TrackingLevel::full_draw_timing))
				log::Info("Timing: heap %s, %u marks this frame, %zu published, %u frames resolved, "
				          "%u abandoned, %u marks dropped, %u stale discarded, frequency %llu",
					timer.IsReady() ? "ready" : "unavailable", timer.MarksThisFrame(), timings.size(),
					timer.ResolvedFrames(), timer.AbandonedFrames(), timer.DroppedMarks(),
					timer.DiscardedMarks(), static_cast<unsigned long long>(timestamp_frequency));
		}

		// 6. GPU preview: one copy into the shared texture, on the game's own queue. The final
		// image is the one the host names (the 3D render of Unreal's main viewport), else the
		// swap chain's.
		capture_buffers.Update(device, frame_index);
		preview.SetFinalImageSource(resource{ viewport_scope.final_image });
		{
			PreviewReadyRecord preview_record = {};
			const resource back_buffer = swapchain != nullptr ? swapchain->get_current_back_buffer() : resource{};
			if (preview.Update(device, queue, resources, back_buffer, ipc.AppProcessId(), preview_record))
				ipc.PublishPreviewReady(preview_record);
		}

		// 7. Cost of the tool itself, once per second or so.
		if ((frame_index % 60) == 0)
		{
			StatsRecord stats = {};
			stats.frame_index = frame_index;
			stats.bytes_written = ipc.BytesWritten();
			stats.records_written = ipc.RecordsWritten();
			stats.dropped_bytes = ipc.DroppedBytes();
			stats.tracked_shaders = shaders.ShaderCount();
			stats.tracked_resources = resources.AliveCount();
			stats.tracked_pipelines = shaders.PipelineCount();
			stats.shared_texture_bytes_kb = preview.SharedBytes() / 1024;
			ipc.PublishStats(stats);
		}

		ipc.SignalReader();
		ipc.Heartbeat(frame_index, current_level);

		// 8. Where things are, for the in-process interface.
		{
			inprocess::StatusSnapshot status;
			status.api = api;
			status.level = level.load(std::memory_order_relaxed);
			status.standalone_connected = ipc.IsClientConnected() && ipc.AppProcessId() != 0;
			status.standalone_process_id = ipc.AppProcessId();
			status.capture = capture.State();
			status.frame_index = frame_index;
			inprocess::PublishStatus(status);
		}

		last_present_qpc = start_qpc;
		addon_cpu_ms = QpcToMilliseconds(Qpc() - start_qpc);
		frames.AdvanceFrame();
	}

	bool DeviceContext::FindViewportScope(const std::vector<FrameEvent> &events, uint32_t &first_event,
	                                      uint32_t &last_event) const
	{
		const uint32_t begin_id = resources.IdOf(resource{ viewport_scope.begin_marker });
		const uint32_t end_id = resources.IdOf(resource{ viewport_scope.end_marker });
		if (begin_id == 0 || end_id == 0)
			return false;

		// A marker is cleared as a render target: it shows as the target of a binding, a render
		// pass or a clear, whichever the driver path produces.
		auto touches = [](const FrameEvent &event, uint32_t id) {
			if (event.primary_resource == id || event.secondary_resource == id)
				return true;
			return (event.kind == EventKind::bind_render_targets || event.kind == EventKind::begin_render_pass) &&
				(event.b == id || event.c == id || event.d == id);
		};

		size_t begin_at = events.size();
		for (size_t i = 0; i < events.size(); ++i)
			if (touches(events[i], begin_id))
			{
				begin_at = i;
				break;
			}
		for (size_t i = begin_at + 1; i < events.size(); ++i)
			if (touches(events[i], end_id))
			{
				first_event = events[begin_at].index;
				last_event = events[i].index;
				return true;
			}
		return false;
	}

	void DeviceContext::PublishResourceNames(const std::vector<FrameEvent> &events,
	                                         const FrameRecorder::CapturedState &captured)
	{
		std::unordered_map<uint32_t, bool> seen;
		auto publish = [&](uint32_t resource_id) {
			if (resource_id == 0 || !seen.emplace(resource_id, true).second)
				return;
			const resource handle = resources.HandleOf(resource_id);
			if (handle.handle == 0)
				return;
			std::string name = ReadNativeName(device, handle);
			if (name.empty())
				return;
			std::string &published = published_names[resource_id];
			if (published == name)
				return;
			published = std::move(name);
			ipc.PublishResourceName(resource_id, published);
		};

		for (const FrameEvent &event : events)
		{
			publish(event.primary_resource);
			publish(event.secondary_resource);
			// Targets 1 to 3 travel in the counters of a binding event, and only there.
			if (event.kind == EventKind::bind_render_targets || event.kind == EventKind::begin_render_pass)
			{
				publish(event.b);
				publish(event.c);
				publish(event.d);
			}
		}
		for (const DrawBinding &binding : captured.bindings)
			publish(binding.resource_id);
	}

	// Sends the pipelines whose state the standalone has not seen yet. A game creates its
	// pipelines long before anyone arms a capture, so the list is collected as they are created
	// and drained here, the first time a capture needs it.
	void DeviceContext::PublishPipelineStates()
	{
		std::vector<PipelineStateRecord> to_send;
		{
			std::lock_guard<std::mutex> lock(pipeline_state_mutex);
			if (pipeline_states.empty())
				return;
			to_send.swap(pipeline_states);
		}

		for (const PipelineStateRecord &record : to_send)
			ipc.PublishPipelineState(record);

		// Kept so a second capture in the same session does not have to be told again.
		std::lock_guard<std::mutex> lock(pipeline_state_mutex);
		pending_pipeline_states.insert(pending_pipeline_states.end(), to_send.begin(), to_send.end());
	}

	DeviceContext *ContextOf(reshade::api::device *device)
	{
		return device != nullptr ? device->get_private_data<DeviceContext>() : nullptr;
	}

	DeviceContext *ContextOf(reshade::api::command_list *command_list)
	{
		return command_list != nullptr ? ContextOf(command_list->get_device()) : nullptr;
	}

	DeviceContext *ContextOf(reshade::api::command_queue *queue)
	{
		return queue != nullptr ? ContextOf(queue->get_device()) : nullptr;
	}

	// -----------------------------------------------------------------------------------------
	// ReShade event handlers
	// -----------------------------------------------------------------------------------------

	namespace
	{
		void OnInitDevice(device *device)
		{
			device->create_private_data<DeviceContext>(device);
			inprocess::DeviceCreated();
		}

		void OnDestroyDevice(device *device)
		{
			device->destroy_private_data<DeviceContext>();
			inprocess::DeviceDestroyed();
		}

		void OnInitCommandList(command_list *command_list)
		{
			CommandListState *state = command_list->create_private_data<CommandListState>();
			state->queue_index = static_cast<uint8_t>(g_next_queue_index.fetch_add(1) & 0xFF);
		}

		void OnDestroyCommandList(command_list *command_list)
		{
			if (DeviceContext *context = ContextOf(command_list))
				context->UnregisterImmediateState(StateOf(command_list));
			command_list->destroy_private_data<CommandListState>();
		}

		void OnInitCommandQueue(command_queue *queue)
		{
			DeviceContext *context = ContextOf(queue);
			if (context == nullptr)
				return;

			// A D3D12 game creates several queues — direct, compute, copy — and this runs once per
			// queue. A copy queue commonly reports a frequency of zero, and taking it would set the
			// session's frequency to zero after a good one had already been published, which makes
			// every timing unconvertible and the timeline report that there are none. So a zero is
			// never taken, and a graphics queue outranks whatever was taken before it.
			const command_queue_type type = queue->get_type();
			const bool is_graphics = (type & command_queue_type::graphics) == command_queue_type::graphics;
			const uint64_t frequency = queue->get_timestamp_frequency();
			if (frequency != 0 && (context->timestamp_frequency == 0 ||
			                       (is_graphics && !context->timestamp_frequency_from_graphics)))
			{
				context->timestamp_frequency = frequency;
				context->timestamp_frequency_from_graphics = is_graphics;
				context->ipc.SetTimestampFrequency(frequency);
			}
			log::Info("Queue initialized (type 0x%X, timestamp frequency %llu, session frequency %llu)",
				static_cast<unsigned int>(type), static_cast<unsigned long long>(frequency),
				static_cast<unsigned long long>(context->timestamp_frequency));

			context->ipc.PublishSessionInfo(context->level.load(std::memory_order_relaxed));

			// In D3D11 the immediate context is both queue and command list: its events are never
			// merged by execute_command_list, so they have to be collected at present time.
			if (command_list *immediate = queue->get_immediate_command_list())
			{
				if (CommandListState *state = StateOf(immediate))
					context->RegisterImmediateState(state);
			}
		}

		void OnDestroyCommandQueue(command_queue *queue)
		{
			DeviceContext *context = ContextOf(queue);
			if (context == nullptr)
				return;
			if (command_list *immediate = queue->get_immediate_command_list())
			{
				if (CommandListState *state = StateOf(immediate))
					context->UnregisterImmediateState(state);
			}
		}

		void OnInitSwapchain(swapchain *swapchain, bool /*resize*/)
		{
			DeviceContext *context = ContextOf(swapchain->get_device());
			if (context == nullptr)
				return;

			const uint32_t count = swapchain->get_back_buffer_count();
			for (uint32_t i = 0; i < count; ++i)
				context->resources.MarkBackBuffer(swapchain->get_back_buffer(i));
		}

		void OnInitPipeline(device *device, pipeline_layout layout, uint32_t subobject_count,
		                    const pipeline_subobject *subobjects, pipeline pipeline)
		{
			DeviceContext *context = ContextOf(device);
			if (context == nullptr || pipeline.handle == 0)
				return;

			uint32_t shader_ids[kMaxPipelineShaders] = {};
			uint32_t shader_count = 0;
			uint32_t stage_mask = 0;
			bool is_compute = false;

			// The fixed function state is read once here, while the subobjects are still valid,
			// and kept. It is only ever sent during a deep capture, but it has to be collected
			// now: by the time a capture is armed this pipeline is long since created.
			PipelineStateRecord pipeline_state = {};
			const bool has_state = ExtractPipelineState(0, subobject_count, subobjects, pipeline_state);

			for (uint32_t i = 0; i < subobject_count && shader_count < kMaxPipelineShaders; ++i)
			{
				if (!IsShaderSubobject(subobjects[i].type) || subobjects[i].data == nullptr)
					continue;

				const shader_desc *desc = static_cast<const shader_desc *>(subobjects[i].data);
				if (desc->code == nullptr || desc->code_size == 0)
					continue;

				const ShaderStage stage = StageOfSubobject(subobjects[i].type);
				// The byte code pointer is only valid during this callback: RegisterShader copies it.
				const uint32_t shader_id = context->shaders.RegisterShader(desc->code, desc->code_size, stage);
				if (shader_id == 0)
					continue;

				shader_ids[shader_count++] = shader_id;
				stage_mask |= 1u << static_cast<uint32_t>(stage);
				if (stage == ShaderStage::compute)
					is_compute = true;
			}

			if (shader_count == 0)
				return; // blend / rasterizer / input layout state object, nothing to track

			const uint32_t pipeline_id = context->shaders.RegisterPipeline(pipeline.handle, shader_ids,
				shader_count, stage_mask, is_compute);

			// Keep the description so a replacement pipeline can be rebuilt later: ReShade only
			// lets a shader be substituted at creation time, which is far too early to be useful.
			{
				PipelineBlueprint blueprint;
				blueprint.Capture(subobject_count, subobjects, layout,
					std::vector<uint32_t>(shader_ids, shader_ids + shader_count));
				context->shaders.SetBlueprint(pipeline_id, std::move(blueprint));
			}

			if (has_state)
			{
				pipeline_state.pipeline_id = pipeline_id;
				std::lock_guard<std::mutex> lock(context->pipeline_state_mutex);
				context->pipeline_states.push_back(pipeline_state);
			}

			PipelineRecord record;
			if (context->shaders.CopyPipelineById(pipeline_id, record))
				context->control.OnPipelineRegistered(record);
		}

		void OnDestroyPipeline(device *device, pipeline pipeline)
		{
			if (DeviceContext *context = ContextOf(device))
				context->shaders.UnregisterPipeline(pipeline.handle);
		}

		void OnInitResource(device *device, const resource_desc &desc, const subresource_data * /*initial_data*/,
		                    resource_usage /*initial_state*/, resource resource)
		{
			if (DeviceContext *context = ContextOf(device))
				context->resources.Register(device, resource, desc, context->frames.FrameIndex(),
					context->frames.EventCount());
		}

		void OnDestroyResource(device *device, resource resource)
		{
			if (DeviceContext *context = ContextOf(device))
				context->resources.Unregister(resource, context->frames.FrameIndex(),
					context->frames.EventCount());
		}

		void OnInitResourceView(device *device, resource resource, resource_usage /*usage*/,
		                        const resource_view_desc & /*desc*/, resource_view view)
		{
			if (DeviceContext *context = ContextOf(device))
				context->resources.RegisterView(view, resource);
		}

		void OnDestroyResourceView(device *device, resource_view view)
		{
			if (DeviceContext *context = ContextOf(device))
				context->resources.UnregisterView(view);
		}

		void MarkTiming(DeviceContext &context, command_list *cmd, CommandListState &state,
		                const FrameEvent &event)
		{
			const TrackingLevel level = context.level.load(std::memory_order_relaxed);
			const bool per_draw = context.capture.WantsPerDrawTiming();
			if (!per_draw && level != TrackingLevel::full_draw_timing && level != TrackingLevel::pass_timing)
				return;
			if (!context.timer.IsReady())
				return;

			// Pass timing only marks when the render target set changed, which is where a pass
			// boundary is; full draw timing marks every command.
			if (!per_draw && level == TrackingLevel::pass_timing)
			{
				const uint64_t signature = (static_cast<uint64_t>(event.primary_resource) << 32) |
					event.secondary_resource;
				if (signature == state.timed_target_signature)
					return;
				state.timed_target_signature = signature;
			}

			const TimingMark mark = context.timer.Mark(cmd, static_cast<uint32_t>(state.events.size()) - 1,
				event.pipeline_id);
			if (mark.IsValid())
				state.timing_marks.push_back(mark);
			context.timed_marks.fetch_add(1, std::memory_order_relaxed);
		}

		void OnBindPipeline(command_list *command_list, pipeline_stage stages, pipeline pipeline)
		{
			if (g_inside_replacement)
				return;

			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return;

			const uint32_t pipeline_id = context->shaders.PipelineId(pipeline.handle);
			if (pipeline_id == 0)
				return;

			if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(pipeline_stage::compute_shader)) != 0)
			{
				state->compute_pipeline_id = pipeline_id;
				state->compute_pipeline_native = pipeline.handle;
				state->compute_pipeline_stages = static_cast<uint32_t>(stages);
			}
			if ((static_cast<uint32_t>(stages) &
			     static_cast<uint32_t>(pipeline_stage::all_graphics)) != 0)
			{
				state->graphics_pipeline_id = pipeline_id;
				state->graphics_pipeline_native = pipeline.handle;
				state->graphics_pipeline_stages = static_cast<uint32_t>(stages);
			}
		}

		// ------------------------------------------------------------------------------------
		// Deep capture callbacks.
		//
		// These are registered once and stay registered, because ReShade does not let an add-on
		// add and remove event handlers per frame without racing the game's own threads. What
		// makes them free in runtime mode is the first line of each: a relaxed atomic load that
		// says no capture is running, and an immediate return.
		// ------------------------------------------------------------------------------------

		// Returns the pair only when a deep capture is actually recording.
		bool CapturingState(command_list *command_list, DeviceContext *&context, CommandListState *&state)
		{
			context = ContextOf(command_list);
			if (context == nullptr || !context->capture.Recording())
				return false;
			state = StateOf(command_list);
			return state != nullptr;
		}

		void OnPushDescriptors(command_list *command_list, shader_stage stages, pipeline_layout /*layout*/,
		                       uint32_t /*layout_param*/, const descriptor_table_update &update)
		{
			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state) || !context->capture.WantsBindings())
				return;
			ApplyDescriptorUpdate(*state, context->resources, stages, update);
		}

		// Direct3D 12 root tables and Vulkan descriptor sets are bound by handle. The add-on API
		// hands us the handle, not the heap behind it, so there is nothing here to read. Rather
		// than let the draw look unbound, the command list is marked and the interface says the
		// list is incomplete. This is a real limit of capturing through ReShade, not an omission.
		void OnBindDescriptorTables(command_list *command_list, shader_stage /*stages*/,
		                            pipeline_layout /*layout*/, uint32_t /*first*/, uint32_t count,
		                            const descriptor_table *tables, uint32_t /*dynamic_offset_count*/,
		                            const uint32_t * /*dynamic_offsets*/)
		{
			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state) || !context->capture.WantsBindings())
				return;
			if (count != 0 && tables != nullptr)
				state->tables_unresolved = true;
		}

		void OnBindVertexBuffers(command_list *command_list, uint32_t first, uint32_t count,
		                         const resource *buffers, const uint64_t *offsets, const uint32_t * /*strides*/)
		{
			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state) || !context->capture.WantsBindings())
				return;
			ApplyVertexBuffers(*state, context->resources, first, count, buffers, offsets);
		}

		void OnBindIndexBuffer(command_list *command_list, resource buffer, uint64_t offset,
		                       uint32_t index_size)
		{
			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state) || !context->capture.WantsBindings())
				return;
			ApplyIndexBuffer(*state, context->resources, buffer, offset, index_size);
		}

		void OnBindViewports(command_list *command_list, uint32_t first, uint32_t count,
		                     const viewport *viewports)
		{
			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state))
				return;
			ApplyViewports(*state, first, count, viewports);
		}

		void OnBindScissorRects(command_list *command_list, uint32_t first, uint32_t count,
		                        const rect *rects)
		{
			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state))
				return;
			ApplyScissorRects(*state, first, count, rects);
		}

		void OnBindPipelineStates(command_list *command_list, uint32_t count,
		                          const dynamic_state *states, const uint32_t *values)
		{
			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state))
				return;
			ApplyPipelineStates(*state, count, states, values);
		}

		void OnBarrier(command_list *command_list, uint32_t count, const resource *resources,
		               const resource_usage *old_states, const resource_usage *new_states)
		{
			// First, and whatever the capture is doing: one atomic load when nothing is previewed.
			PreviewBridge::NoteBarriers(count, resources, new_states);

			DeviceContext *context = nullptr;
			CommandListState *state = nullptr;
			if (!CapturingState(command_list, context, state) || !context->capture.WantsBarriers())
				return;

			// The barrier is an event in its own right, so it appears on the timeline between the
			// commands it separates, and the transitions hang off it.
			state->Push(EventKind::barrier);
			RecordBarriers(*state, context->capture, context->resources, count, resources, old_states,
				new_states);
		}

		void OnBindRenderTargets(command_list *command_list, uint32_t count, const resource_view *rtvs,
		                         resource_view dsv)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return;

			state->render_target_count = count < CommandListState::kMaxRenderTargets
				? count : CommandListState::kMaxRenderTargets;
			for (uint32_t i = 0; i < state->render_target_count; ++i)
				state->render_target_ids[i] = context->resources.IdOfView(rtvs[i]);
			state->depth_target_id = context->resources.IdOfView(dsv);

			// Draw events only carry render target 0. Recording the binding itself is what lets
			// the standalone see a multiple render target setup, and therefore a GBuffer.
			FrameEvent &event = state->Push(EventKind::bind_render_targets);
			event.primary_resource = state->PrimaryRenderTarget();
			event.secondary_resource = state->depth_target_id;
			event.a = state->render_target_count;
			event.b = state->render_target_count > 1 ? state->render_target_ids[1] : 0;
			event.c = state->render_target_count > 2 ? state->render_target_ids[2] : 0;
			event.d = state->render_target_count > 3 ? state->render_target_ids[3] : 0;
		}

		bool OnBeginRenderPass(command_list *command_list, uint32_t count,
		                       const render_pass_render_target_desc *rts,
		                       const render_pass_depth_stencil_desc *ds, render_pass_flags /*flags*/)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			state->render_target_count = count < CommandListState::kMaxRenderTargets
				? count : CommandListState::kMaxRenderTargets;
			for (uint32_t i = 0; i < state->render_target_count; ++i)
				state->render_target_ids[i] = context->resources.IdOfView(rts[i].view);
			state->depth_target_id = ds != nullptr ? context->resources.IdOfView(ds->view) : 0;
			state->in_render_pass = true;

			FrameEvent &event = state->Push(EventKind::begin_render_pass);
			event.primary_resource = state->PrimaryRenderTarget();
			event.secondary_resource = state->depth_target_id;
			event.a = state->render_target_count;
			event.b = state->render_target_count > 1 ? state->render_target_ids[1] : 0;
			event.c = state->render_target_count > 2 ? state->render_target_ids[2] : 0;
			event.d = state->render_target_count > 3 ? state->render_target_ids[3] : 0;
			return false;
		}

		bool OnEndRenderPass(command_list *command_list)
		{
			if (CommandListState *state = StateOf(command_list))
			{
				state->Push(EventKind::end_render_pass);
				state->in_render_pass = false;
			}
			return false;
		}

		bool OnDraw(command_list *command_list, uint32_t vertex_count, uint32_t instance_count,
		            uint32_t first_vertex, uint32_t first_instance)
		{
			if (g_inside_replacement)
				return false;

			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::draw);
			FillFromBindings(event, *state, false);
			CaptureStateOfLastCommand(*context, *state, false);
			MarkTiming(*context, command_list, *state, event);
			event.a = vertex_count;
			event.b = instance_count;
			event.c = first_vertex;
			event.d = first_instance;

			if (context->control.AnythingActive() && context->control.IsPipelineDisabled(event.pipeline_id))
			{
				event.flags |= kEventSkipped;
				context->skipped_draws.fetch_add(1, std::memory_order_relaxed);
				return true;
			}

			if (context->control.AnyReplacement())
			{
				const uint64_t replacement = context->control.ReplacementFor(event.pipeline_id);
				if (IssueThroughReplacement(command_list, *state, false, replacement, [&]() {
					command_list->draw(vertex_count, instance_count, first_vertex, first_instance);
				}))
				{
					event.flags |= kEventReplaced;
					return true;
				}
			}
			return false;
		}

		bool OnDrawIndexed(command_list *command_list, uint32_t index_count, uint32_t instance_count,
		                   uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
		{
			if (g_inside_replacement)
				return false;

			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::draw_indexed);
			FillFromBindings(event, *state, false);
			CaptureStateOfLastCommand(*context, *state, false);
			MarkTiming(*context, command_list, *state, event);
			event.a = index_count;
			event.b = instance_count;
			event.c = first_index;
			event.d = static_cast<uint32_t>(vertex_offset);
			(void)first_instance;

			if (context->control.AnythingActive() && context->control.IsPipelineDisabled(event.pipeline_id))
			{
				event.flags |= kEventSkipped;
				context->skipped_draws.fetch_add(1, std::memory_order_relaxed);
				return true;
			}

			if (context->control.AnyReplacement())
			{
				const uint64_t replacement = context->control.ReplacementFor(event.pipeline_id);
				if (IssueThroughReplacement(command_list, *state, false, replacement, [&]() {
					command_list->draw_indexed(index_count, instance_count, first_index, vertex_offset,
						first_instance);
				}))
				{
					event.flags |= kEventReplaced;
					return true;
				}
			}
			return false;
		}

		bool OnDispatch(command_list *command_list, uint32_t group_x, uint32_t group_y, uint32_t group_z)
		{
			if (g_inside_replacement)
				return false;

			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::dispatch);
			FillFromBindings(event, *state, true);
			CaptureStateOfLastCommand(*context, *state, true);
			MarkTiming(*context, command_list, *state, event);
			event.a = group_x;
			event.b = group_y;
			event.c = group_z;

			if (context->control.AnythingActive() && context->control.IsPipelineDisabled(event.pipeline_id))
			{
				event.flags |= kEventSkipped;
				return true;
			}

			if (context->control.AnyReplacement())
			{
				const uint64_t replacement = context->control.ReplacementFor(event.pipeline_id);
				if (IssueThroughReplacement(command_list, *state, true, replacement, [&]() {
					command_list->dispatch(group_x, group_y, group_z);
				}))
				{
					event.flags |= kEventReplaced;
					return true;
				}
			}
			return false;
		}

		bool OnDispatchMesh(command_list *command_list, uint32_t group_x, uint32_t group_y, uint32_t group_z)
		{
			CommandListState *state = StateOf(command_list);
			if (state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::dispatch_mesh);
			FillFromBindings(event, *state, false);
			if (DeviceContext *context = ContextOf(command_list))
				CaptureStateOfLastCommand(*context, *state, false);
			event.a = group_x;
			event.b = group_y;
			event.c = group_z;
			return false;
		}

		bool OnDrawOrDispatchIndirect(command_list *command_list, indirect_command type, resource buffer,
		                              uint64_t offset, uint32_t draw_count, uint32_t stride)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			const bool compute = type == indirect_command::dispatch;
			FrameEvent &event = state->Push(compute ? EventKind::dispatch_indirect : EventKind::draw_indirect);
			FillFromBindings(event, *state, compute);
			CaptureStateOfLastCommand(*context, *state, compute);
			event.flags |= kEventIndirect;
			event.a = draw_count;
			event.b = stride;
			event.c = static_cast<uint32_t>(offset);
			event.d = context->resources.IdOf(buffer);

			if (context->control.AnythingActive() && context->control.IsPipelineDisabled(event.pipeline_id))
			{
				event.flags |= kEventSkipped;
				return true;
			}
			return false;
		}

		bool OnCopyResource(command_list *command_list, resource source, resource dest)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::copy_resource);
			event.primary_resource = context->resources.IdOf(dest);
			event.secondary_resource = context->resources.IdOf(source);
			return false;
		}

		bool OnCopyTextureRegion(command_list *command_list, resource source, uint32_t source_subresource,
		                         const subresource_box * /*source_box*/, resource dest, uint32_t dest_subresource,
		                         const subresource_box * /*dest_box*/, filter_mode /*filter*/)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::copy_texture_region);
			event.primary_resource = context->resources.IdOf(dest);
			event.secondary_resource = context->resources.IdOf(source);
			event.a = source_subresource;
			event.b = dest_subresource;
			return false;
		}

		bool OnResolveTextureRegion(command_list *command_list, resource source, uint32_t /*source_subresource*/,
		                            const subresource_box * /*source_box*/, resource dest, uint32_t /*dest_subresource*/,
		                            uint32_t /*dest_x*/, uint32_t /*dest_y*/, uint32_t /*dest_z*/, format /*format*/)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::resolve);
			event.primary_resource = context->resources.IdOf(dest);
			event.secondary_resource = context->resources.IdOf(source);
			return false;
		}

		bool OnClearRenderTargetView(command_list *command_list, resource_view rtv, const float[4],
		                             uint32_t /*rect_count*/, const rect * /*rects*/)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::clear_render_target);
			event.primary_resource = context->resources.IdOfView(rtv);
			return false;
		}

		bool OnClearDepthStencilView(command_list *command_list, resource_view dsv, const float * /*depth*/,
		                             const uint8_t * /*stencil*/, uint32_t /*rect_count*/, const rect * /*rects*/)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::clear_depth_stencil);
			event.primary_resource = context->resources.IdOfView(dsv);
			return false;
		}

		bool OnClearUnorderedAccessViewFloat(command_list *command_list, resource_view uav, const float[4],
		                                     uint32_t /*rect_count*/, const rect * /*rects*/)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::clear_unordered_access);
			event.primary_resource = context->resources.IdOfView(uav);
			return false;
		}

		bool OnClearUnorderedAccessViewUint(command_list *command_list, resource_view uav, const uint32_t[4],
		                                    uint32_t /*rect_count*/, const rect * /*rects*/)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::clear_unordered_access);
			event.primary_resource = context->resources.IdOfView(uav);
			return false;
		}

		bool OnGenerateMipmaps(command_list *command_list, resource_view srv)
		{
			DeviceContext *context = ContextOf(command_list);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return false;

			FrameEvent &event = state->Push(EventKind::generate_mipmaps);
			event.primary_resource = context->resources.IdOfView(srv);
			return false;
		}

		void OnResetCommandList(command_list *command_list)
		{
			if (CommandListState *state = StateOf(command_list))
				state->Reset();
		}

		void OnExecuteCommandList(command_queue *queue, command_list *command_list)
		{
			DeviceContext *context = ContextOf(queue);
			CommandListState *state = StateOf(command_list);
			if (context == nullptr || state == nullptr)
				return;
			context->MergeCommandList(*state);
		}

		void OnExecuteSecondaryCommandList(command_list *primary_list, command_list *secondary_list)
		{
			DeviceContext *context = ContextOf(primary_list);
			CommandListState *state = StateOf(secondary_list);
			if (context == nullptr || state == nullptr)
				return;
			context->MergeCommandList(*state);
		}

		void OnPresent(command_queue *queue, swapchain *swapchain, const rect * /*source*/,
		               const rect * /*dest*/, uint32_t /*dirty_rect_count*/, const rect * /*dirty_rects*/)
		{
			if (DeviceContext *context = ContextOf(queue))
				if (context->IsPrimaryPresent(swapchain))
					context->EndFrame(queue, swapchain);
		}
	}

	void RegisterDeviceEvents()
	{
		reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
		reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
		reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
		reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
		reshade::register_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue);
		reshade::register_event<reshade::addon_event::destroy_command_queue>(OnDestroyCommandQueue);
		reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);

		reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
		reshade::register_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);
		reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
		reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
		reshade::register_event<reshade::addon_event::init_resource_view>(OnInitResourceView);
		reshade::register_event<reshade::addon_event::destroy_resource_view>(OnDestroyResourceView);

		reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

		// Deep capture. Registered permanently; each one returns immediately unless a capture is
		// running, so runtime mode pays a predictable branch and nothing more.
		reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
		reshade::register_event<reshade::addon_event::bind_descriptor_tables>(OnBindDescriptorTables);
		reshade::register_event<reshade::addon_event::bind_vertex_buffers>(OnBindVertexBuffers);
		reshade::register_event<reshade::addon_event::bind_index_buffer>(OnBindIndexBuffer);
		reshade::register_event<reshade::addon_event::bind_viewports>(OnBindViewports);
		reshade::register_event<reshade::addon_event::bind_scissor_rects>(OnBindScissorRects);
		reshade::register_event<reshade::addon_event::bind_pipeline_states>(OnBindPipelineStates);
		reshade::register_event<reshade::addon_event::barrier>(OnBarrier);
		reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargets);
		reshade::register_event<reshade::addon_event::begin_render_pass>(OnBeginRenderPass);
		reshade::register_event<reshade::addon_event::end_render_pass>(OnEndRenderPass);

		reshade::register_event<reshade::addon_event::draw>(OnDraw);
		reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
		reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
		reshade::register_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh);
		reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

		reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
		reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
		reshade::register_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
		reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
		reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(OnClearDepthStencilView);
		reshade::register_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccessViewFloat);
		reshade::register_event<reshade::addon_event::clear_unordered_access_view_uint>(OnClearUnorderedAccessViewUint);
		reshade::register_event<reshade::addon_event::generate_mipmaps>(OnGenerateMipmaps);

		reshade::register_event<reshade::addon_event::reset_command_list>(OnResetCommandList);
		reshade::register_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);
		reshade::register_event<reshade::addon_event::execute_secondary_command_list>(OnExecuteSecondaryCommandList);
		reshade::register_event<reshade::addon_event::present>(OnPresent);
	}

	void UnregisterDeviceEvents()
	{
		reshade::unregister_event<reshade::addon_event::present>(OnPresent);
		reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(OnExecuteSecondaryCommandList);
		reshade::unregister_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);
		reshade::unregister_event<reshade::addon_event::reset_command_list>(OnResetCommandList);

		reshade::unregister_event<reshade::addon_event::barrier>(OnBarrier);
		reshade::unregister_event<reshade::addon_event::bind_pipeline_states>(OnBindPipelineStates);
		reshade::unregister_event<reshade::addon_event::bind_scissor_rects>(OnBindScissorRects);
		reshade::unregister_event<reshade::addon_event::bind_viewports>(OnBindViewports);
		reshade::unregister_event<reshade::addon_event::bind_index_buffer>(OnBindIndexBuffer);
		reshade::unregister_event<reshade::addon_event::bind_vertex_buffers>(OnBindVertexBuffers);
		reshade::unregister_event<reshade::addon_event::bind_descriptor_tables>(OnBindDescriptorTables);
		reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);

		reshade::unregister_event<reshade::addon_event::generate_mipmaps>(OnGenerateMipmaps);
		reshade::unregister_event<reshade::addon_event::clear_unordered_access_view_uint>(OnClearUnorderedAccessViewUint);
		reshade::unregister_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccessViewFloat);
		reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(OnClearDepthStencilView);
		reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
		reshade::unregister_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);
		reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
		reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);

		reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);
		reshade::unregister_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh);
		reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
		reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
		reshade::unregister_event<reshade::addon_event::draw>(OnDraw);

		reshade::unregister_event<reshade::addon_event::end_render_pass>(OnEndRenderPass);
		reshade::unregister_event<reshade::addon_event::begin_render_pass>(OnBeginRenderPass);
		reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargets);
		reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

		reshade::unregister_event<reshade::addon_event::destroy_resource_view>(OnDestroyResourceView);
		reshade::unregister_event<reshade::addon_event::init_resource_view>(OnInitResourceView);
		reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
		reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
		reshade::unregister_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);
		reshade::unregister_event<reshade::addon_event::init_pipeline>(OnInitPipeline);

		reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
		reshade::unregister_event<reshade::addon_event::destroy_command_queue>(OnDestroyCommandQueue);
		reshade::unregister_event<reshade::addon_event::init_command_queue>(OnInitCommandQueue);
		reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
		reshade::unregister_event<reshade::addon_event::init_command_list>(OnInitCommandList);
		reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
		reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
	}
}
