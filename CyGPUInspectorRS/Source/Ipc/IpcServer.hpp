// CyGPUInspectorRS — IPC server: event ring, control pipe and session advertisement.
//
// Everything here is designed so the game thread never waits on the standalone: the ring drops
// records when it is full, the pipe runs on its own thread, and the session slot is only a
// heartbeat.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Tracking/ResourceTracker.hpp"
#include "Tracking/ShaderTracker.hpp"

#include <CyGPUInspectorCore/ControlPipe.hpp>
#include <CyGPUInspectorCore/Protocol.hpp>
#include <CyGPUInspectorCore/RingBuffer.hpp>
#include <CyGPUInspectorCore/SessionDirectory.hpp>
#include <CyGPUInspectorCore/SharedMemory.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cygi
{
	class IpcServer
	{
	public:
		~IpcServer();

		bool Start(uint32_t device_index, GraphicsApi api, const char *process_name,
		           uint32_t capability_flags, uint64_t timestamp_frequency, const char *adapter_name,
		           size_t ring_capacity = kDefaultRingCapacity);
		void Stop();

		bool IsRunning() const { return m_writer.IsValid(); }
		bool IsClientConnected() const { return m_pipe.IsClientConnected(); }
		uint32_t AppProcessId() const { return m_app_process_id; }
		void SetAppProcessId(uint32_t process_id) { m_app_process_id = process_id; }
		// The GPU timestamp frequency is only known once a command queue exists.
		void SetTimestampFrequency(uint64_t frequency) { m_timestamp_frequency = frequency; }

		void PublishSessionInfo(TrackingLevel level);
		void PublishShader(const ShaderRecord &record, const uint8_t *code, uint32_t code_size);
		void PublishPipeline(const PipelineRecord &record);
		void PublishResource(const ResourceRecord &record);
		void PublishResourceGone(uint32_t resource_id, uint64_t frame_index, uint32_t event_index);
		void PublishResourceName(uint32_t resource_id, const std::string &name);
		void PublishFrameBegin(uint64_t frame_index);
		void PublishFrameEvents(uint64_t frame_index, const std::vector<FrameEvent> &events);
		void PublishFrameEnd(const FrameEndRecord &record);
		void PublishStats(const StatsRecord &record);
		void PublishTimings(uint64_t frame_index, const std::vector<TimingResult> &timings);
		void PublishFrameGpuSpan(uint64_t frame_index, uint64_t gpu_begin, uint64_t gpu_end);
		void PublishLog(LogLevel level, const char *text);
		void PublishPreviewReady(const PreviewReadyRecord &record);
		void PublishCaptureBuffer(const CaptureBufferRecord &record);
		void PublishCaptureScope(const CaptureScopeRecord &record);

		// Deep capture only. Each of these is written once per command, so they are the reason
		// the deep capture is a one shot rather than a setting.
		void PublishDrawState(const DrawStateRecord &record, const DrawBinding *bindings);
		void PublishBarriers(const BarrierSetRecord &record, const BarrierEntry *entries);
		void PublishPipelineState(const PipelineStateRecord &record);
		void PublishCaptureState(const CaptureStateRecord &record);

		// Wakes the standalone once per frame instead of once per record.
		void SignalReader();
		void Heartbeat(uint64_t frame_index, TrackingLevel level);

		bool PopControlMessage(ControlMessage &out) { return m_pipe.PopMessage(out); }
		ControlPipeServer &Pipe() { return m_pipe; }

		uint64_t BytesWritten() const { return m_writer.BytesWritten(); }
		uint64_t RecordsWritten() const { return m_writer.RecordsWritten(); }
		uint64_t DroppedBytes() const { return m_writer.DroppedBytes(); }
		uint64_t DroppedRecords() const { return m_writer.DroppedRecords(); }

	private:
		SharedMemory m_memory;
		RingWriter m_writer;
		SharedEvent m_signal;
		ControlPipeServer m_pipe;
		SessionPublisher m_session;

		uint32_t m_device_index = 0;
		uint32_t m_app_process_id = 0;
		GraphicsApi m_api = GraphicsApi::unknown;
		uint32_t m_capability_flags = 0;
		uint64_t m_timestamp_frequency = 0;
		std::string m_process_name;
		std::string m_adapter_name;
	};
}
