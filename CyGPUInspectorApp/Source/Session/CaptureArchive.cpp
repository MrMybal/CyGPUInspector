// CyGPUInspectorApp — saving and reopening a frame without the game.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CaptureArchive.hpp"

#include <CyGPUInspectorCore/Format.hpp>
#include <CyGPUInspectorCore/Json.hpp>
#include <CyGPUInspectorCore/Version.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_set>

namespace cygi
{
	namespace
	{
		// 1: events, shaders, resources, timings.
		// 2: adds the deep capture side files. A version 1 capture still opens: the deep files
		//    are simply absent, which is exactly what a runtime capture looks like anyway.
		constexpr uint32_t kCaptureVersion = 2;
		constexpr uint32_t kOldestReadableCaptureVersion = 1;

		std::filesystem::path ExecutableDirectory()
		{
			wchar_t path[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, path, MAX_PATH);
			return std::filesystem::path(path).parent_path();
		}

		std::string Timestamp(bool for_a_file_name)
		{
			SYSTEMTIME now = {};
			GetLocalTime(&now);

			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), for_a_file_name ? "%04u-%02u-%02u_%02u-%02u-%02u"
			                                                      : "%04u-%02u-%02u %02u:%02u:%02u",
				now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
			return buffer;
		}

		bool WriteFile(const std::filesystem::path &path, const void *data, size_t size, std::string &error)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				error = "could not write " + path.filename().string();
				return false;
			}
			if (size != 0)
				file.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
			return file.good();
		}

		bool ReadFile(const std::filesystem::path &path, std::vector<uint8_t> &out)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;

			const std::streamsize size = file.tellg();
			file.seekg(0);
			out.resize(static_cast<size_t>(size));
			return size == 0 || file.read(reinterpret_cast<char *>(out.data()), size).good();
		}

		// Hands one synthetic record to the model, exactly as the IPC reader would.
		void Apply(SessionModel &model, RecordType type, const void *fixed, uint32_t fixed_size,
		           const void *blob = nullptr, uint32_t blob_size = 0)
		{
			std::vector<uint8_t> payload(fixed_size + blob_size);
			if (fixed_size != 0)
				std::memcpy(payload.data(), fixed, fixed_size);
			if (blob_size != 0)
				std::memcpy(payload.data() + fixed_size, blob, blob_size);

			RecordHeader header = {};
			header.size = static_cast<uint32_t>(sizeof(RecordHeader) + payload.size());
			header.type = type;

			model.ApplyRecord(header, payload.data(), static_cast<uint32_t>(payload.size()));
		}
	}

	std::string CaptureInfo::Describe() const
	{
		char buffer[256];
		std::snprintf(buffer, sizeof(buffer), "%s frame %llu - %u draws, %u shaders (%s)",
			process_name.c_str(), static_cast<unsigned long long>(frame_index), draw_count,
			shader_count, created.c_str());
		return buffer;
	}

	std::filesystem::path CaptureArchive::DefaultRoot()
	{
		return ExecutableDirectory() / "Captures";
	}

	std::filesystem::path CaptureArchive::SuggestDirectory(const std::string &process_name)
	{
		std::string name = process_name.empty() ? "Capture" : process_name;
		const size_t dot = name.rfind('.');
		if (dot != std::string::npos)
			name = name.substr(0, dot);

		for (char &c : name)
			if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
			    c == '>' || c == '|' || c == ' ')
				c = '_';

		return DefaultRoot() / (name + "_" + Timestamp(true));
	}

	bool CaptureArchive::Save(const SessionModel &model, const FrameGraph &graph,
	                          const std::filesystem::path &directory, std::string &error)
	{
		std::error_code code;
		std::filesystem::create_directories(directory / "shaders", code);
		if (code)
		{
			error = "could not create the capture directory: " + code.message();
			return false;
		}

		const FrameInfo &frame = model.LastFrame();

		// Only the shaders the frame actually used: a capture is an analysis unit, not a dump of
		// everything the game ever created.
		std::unordered_set<uint32_t> used_shaders;
		for (const FrameEvent &event : frame.events)
		{
			if (event.pipeline_id == 0)
				continue;
			for (uint32_t shader_id : model.ShadersOfPipeline(event.pipeline_id))
				used_shaders.insert(shader_id);
		}

		Json root = Json::Object();
		root["capture_version"] = Json(kCaptureVersion);
		root["tool_version"] = Json(kVersionString);
		root["created"] = Json(Timestamp(false));
		root["protocol_version"] = Json(kProtocolVersion);

		Json session = Json::Object();
		if (model.HasSession())
		{
			const SessionInfoRecord &info = model.Session();
			session["process_name"] = Json(std::string(info.process_name));
			session["api"] = Json(std::string(GraphicsApiName(info.api)));
			session["adapter"] = Json(std::string(info.adapter_name));
			session["addon_version"] = Json(std::string(info.addon_version));
			session["timestamp_frequency"] = Json(info.timestamp_frequency);
			session["capability_flags"] = Json(info.capability_flags);
		}
		root["session"] = session;

		Json frame_json = Json::Object();
		frame_json["index"] = Json(frame.index);
		frame_json["draw_count"] = Json(frame.draw_count);
		frame_json["dispatch_count"] = Json(frame.dispatch_count);
		frame_json["event_count"] = Json(static_cast<uint32_t>(frame.events.size()));
		frame_json["dropped_events"] = Json(frame.dropped_events);
		frame_json["cpu_frame_ms"] = Json(static_cast<double>(frame.cpu_frame_ms));
		frame_json["addon_cpu_ms"] = Json(static_cast<double>(frame.addon_cpu_ms));
		root["frame"] = frame_json;

		Json shaders = Json::Array();
		for (const ShaderInfo &shader : model.Shaders())
		{
			if (shader.id == 0 || used_shaders.count(shader.id) == 0)
				continue;

			Json entry = Json::Object();
			entry["id"] = Json(shader.id);
			entry["signature"] = Json(shader.signature.ToHex());
			entry["semantic_hash"] = Json(shader.semantic_hash.ToHex());
			entry["stage"] = Json(static_cast<uint32_t>(shader.stage));
			entry["format"] = Json(static_cast<uint32_t>(shader.format));
			entry["shader_model"] = Json(shader.shader_model);
			entry["code_size"] = Json(shader.code_size);
			entry["draws"] = Json(shader.draws_this_frame);
			entry["dispatches"] = Json(shader.dispatches_this_frame);
			entry["gpu_ticks"] = Json(shader.gpu_ticks);
			shaders.Push(entry);

			if (!shader.code.empty())
			{
				const std::filesystem::path blob = directory / "shaders" / (shader.signature.ToHex() + ".bin");
				if (!WriteFile(blob, shader.code.data(), shader.code.size(), error))
					return false;
			}
		}
		root["shaders"] = shaders;

		Json pipelines = Json::Array();
		for (const PipelineInfo &pipeline : model.Pipelines())
		{
			if (pipeline.id == 0)
				continue;

			Json entry = Json::Object();
			entry["id"] = Json(pipeline.id);
			entry["is_compute"] = Json(pipeline.is_compute);
			entry["stage_mask"] = Json(pipeline.stage_mask);

			Json ids = Json::Array();
			for (uint32_t shader_id : pipeline.shader_ids)
				ids.Push(Json(shader_id));
			entry["shaders"] = ids;
			pipelines.Push(entry);
		}
		root["pipelines"] = pipelines;

		Json resources = Json::Array();
		for (const ResourceInfo &resource : model.Resources())
		{
			if (resource.id == 0)
				continue;

			Json entry = Json::Object();
			entry["id"] = Json(resource.id);
			entry["kind"] = Json(static_cast<uint32_t>(resource.kind));
			entry["format"] = Json(resource.format);
			entry["format_name"] = Json(std::string(FormatName(resource.format)));
			entry["width"] = Json(resource.width);
			entry["height"] = Json(resource.height);
			entry["depth_or_layers"] = Json(resource.depth_or_layers);
			entry["mip_levels"] = Json(resource.mip_levels);
			entry["samples"] = Json(resource.samples);
			entry["usage_flags"] = Json(resource.usage_flags);
			entry["buffer_size"] = Json(resource.buffer_size);
			entry["created_frame"] = Json(resource.created_frame);
			entry["created_event"] = Json(resource.created_event);
			entry["writes"] = Json(resource.writes_this_frame);
			entry["reads"] = Json(resource.reads_this_frame);
			if (!resource.name.empty())
				entry["name"] = Json(resource.name);
			resources.Push(entry);
		}
		root["resources"] = resources;

		// The derived graph travels with the capture: it is what makes an offline capture
		// navigable rather than a pile of events.
		Json passes = Json::Array();
		double gpu_total = 0.0;
		for (const GraphPass &pass : graph.Passes())
		{
			Json entry = Json::Object();
			entry["id"] = Json(pass.id);
			entry["name"] = Json(pass.name);
			entry["name_origin"] = Json(std::string(PassNameOriginName(pass.origin)));
			entry["confidence"] = Json(static_cast<double>(pass.confidence));
			entry["first_event"] = Json(pass.first_event);
			entry["last_event"] = Json(pass.last_event);
			entry["draw_count"] = Json(pass.draw_count);
			entry["dispatch_count"] = Json(pass.dispatch_count);
			entry["depth_target"] = Json(pass.depth_target);

			Json targets = Json::Array();
			for (uint32_t id : pass.render_targets)
				targets.Push(Json(id));
			entry["render_targets"] = targets;

			Json reads = Json::Array();
			for (uint32_t id : pass.reads)
				reads.Push(Json(id));
			entry["reads"] = reads;

			passes.Push(entry);
		}
		root["passes"] = passes;

		Json timings = Json::Object();
		if (model.Timings().IsValid())
		{
			timings["frame_index"] = Json(model.Timings().frame_index);
			timings["frequency"] = Json(model.Timings().frequency);
			timings["total_ticks"] = Json(model.Timings().total_ticks);
			gpu_total = model.Timings().Milliseconds(model.Timings().total_ticks);
			timings["total_ms"] = Json(gpu_total);

			Json by_event = Json::Array();
			for (const auto &entry : model.Timings().by_event)
			{
				Json pair = Json::Object();
				pair["event"] = Json(entry.first);
				pair["ticks"] = Json(entry.second);
				const auto start = model.Timings().start_by_event.find(entry.first);
				if (start != model.Timings().start_by_event.end())
					pair["start"] = Json(start->second);
				by_event.Push(pair);
			}
			timings["by_event"] = by_event;
		}
		root["timings"] = timings;

		const std::string json = root.Write(2);
		if (!WriteFile(directory / "capture.json", json.data(), json.size(), error))
			return false;

		if (!WriteFile(directory / "events.bin", frame.events.data(),
		               frame.events.size() * sizeof(FrameEvent), error))
			return false;

		// --- deep capture side files ------------------------------------------------------
		// Written only when there is something to write, so a runtime capture stays three files.
		// Each record is followed immediately by its variable part, which is how it came off the
		// wire and how it goes back on when the capture is reopened.
		if (!frame.draw_states.empty())
		{
			std::vector<uint8_t> bytes;
			for (const CommandState &state : frame.draw_states)
			{
				const auto *record = reinterpret_cast<const uint8_t *>(&state.record);
				bytes.insert(bytes.end(), record, record + sizeof(DrawStateRecord));
				const auto *bindings = reinterpret_cast<const uint8_t *>(state.bindings.data());
				bytes.insert(bytes.end(), bindings,
					bindings + state.bindings.size() * sizeof(DrawBinding));
			}
			if (!WriteFile(directory / "drawstates.bin", bytes.data(), bytes.size(), error))
				return false;
		}

		if (!frame.barriers.empty())
		{
			std::vector<uint8_t> bytes;
			for (const BarrierSet &set : frame.barriers)
			{
				BarrierSetRecord record = {};
				record.frame_index = frame.index;
				record.event_index = set.event_index;
				record.count = static_cast<uint32_t>(set.entries.size());

				const auto *header = reinterpret_cast<const uint8_t *>(&record);
				bytes.insert(bytes.end(), header, header + sizeof(BarrierSetRecord));
				const auto *entries = reinterpret_cast<const uint8_t *>(set.entries.data());
				bytes.insert(bytes.end(), entries, entries + set.entries.size() * sizeof(BarrierEntry));
			}
			if (!WriteFile(directory / "barriers.bin", bytes.data(), bytes.size(), error))
				return false;
		}

		return true;
	}

	bool CaptureArchive::ReadInfo(const std::filesystem::path &directory, CaptureInfo &info)
	{
		std::vector<uint8_t> bytes;
		if (!ReadFile(directory / "capture.json", bytes) || bytes.empty())
			return false;

		Json root;
		if (!Json::Parse(std::string(bytes.begin(), bytes.end()), root) || !root.IsObject())
			return false;

		info.path = directory;
		info.process_name = root["session"]["process_name"].AsString();
		info.api = root["session"]["api"].AsString();
		info.created = root["created"].AsString();
		info.tool_version = root["tool_version"].AsString();
		info.frame_index = root["frame"]["index"].AsUInt();
		info.draw_count = root["frame"]["draw_count"].AsUInt32();
		info.dispatch_count = root["frame"]["dispatch_count"].AsUInt32();
		info.event_count = root["frame"]["event_count"].AsUInt32();
		info.shader_count = static_cast<uint32_t>(root["shaders"].Size());
		info.resource_count = static_cast<uint32_t>(root["resources"].Size());
		info.pass_count = static_cast<uint32_t>(root["passes"].Size());
		info.gpu_milliseconds = root["timings"]["total_ms"].AsNumber();
		return true;
	}

	bool CaptureArchive::Load(const std::filesystem::path &directory, SessionModel &model,
	                          CaptureInfo &info, std::string &error)
	{
		std::vector<uint8_t> bytes;
		if (!ReadFile(directory / "capture.json", bytes) || bytes.empty())
		{
			error = "capture.json is missing or unreadable";
			return false;
		}

		Json root;
		std::string parse_error;
		if (!Json::Parse(std::string(bytes.begin(), bytes.end()), root, &parse_error) || !root.IsObject())
		{
			error = "capture.json is not valid JSON: " + parse_error;
			return false;
		}
		const uint32_t version = root["capture_version"].AsUInt32();
		if (version < kOldestReadableCaptureVersion || version > kCaptureVersion)
		{
			error = "this capture was written by another version of CyGPUInspector";
			return false;
		}

		model.Clear();
		ReadInfo(directory, info);

		// --- session --------------------------------------------------------------------------
		{
			SessionInfoRecord record = {};
			record.protocol_version = kProtocolVersion;
			record.timestamp_frequency = root["session"]["timestamp_frequency"].AsUInt();
			record.capability_flags = root["session"]["capability_flags"].AsUInt32();

			const std::string process = root["session"]["process_name"].AsString();
			std::strncpy(record.process_name, process.c_str(), sizeof(record.process_name) - 1);
			const std::string adapter = root["session"]["adapter"].AsString();
			std::strncpy(record.adapter_name, adapter.c_str(), sizeof(record.adapter_name) - 1);
			const std::string addon = root["session"]["addon_version"].AsString();
			std::strncpy(record.addon_version, addon.c_str(), sizeof(record.addon_version) - 1);

			Apply(model, RecordType::session_info, &record, sizeof(record));
		}

		// --- shaders --------------------------------------------------------------------------
		for (const Json &entry : root["shaders"].Items())
		{
			ShaderCodeRecord record = {};
			record.shader_id = entry["id"].AsUInt32();
			record.stage = static_cast<ShaderStage>(entry["stage"].AsUInt32());
			record.format = static_cast<ShaderFormat>(entry["format"].AsUInt32());
			record.shader_model = entry["shader_model"].AsUInt32();
			record.code_size = entry["code_size"].AsUInt32();

			Sha256Digest signature;
			Sha256Digest semantic;
			Sha256Digest::FromHex(entry["signature"].AsString(), signature);
			Sha256Digest::FromHex(entry["semantic_hash"].AsString(), semantic);
			std::memcpy(record.signature, signature.bytes.data(), sizeof(record.signature));
			std::memcpy(record.semantic_hash, semantic.bytes.data(), sizeof(record.semantic_hash));

			std::vector<uint8_t> code;
			ReadFile(directory / "shaders" / (entry["signature"].AsString() + ".bin"), code);
			record.code_size = static_cast<uint32_t>(code.size());

			Apply(model, RecordType::shader_code, &record, sizeof(record), code.data(),
				static_cast<uint32_t>(code.size()));
		}

		// --- pipelines ------------------------------------------------------------------------
		for (const Json &entry : root["pipelines"].Items())
		{
			std::vector<uint32_t> ids;
			for (const Json &id : entry["shaders"].Items())
				ids.push_back(id.AsUInt32());

			PipelineInfoRecord record = {};
			record.pipeline_id = entry["id"].AsUInt32();
			record.is_compute = entry["is_compute"].AsBool() ? 1u : 0u;
			record.stage_mask = entry["stage_mask"].AsUInt32();
			record.shader_count = static_cast<uint32_t>(ids.size());

			Apply(model, RecordType::pipeline_info, &record, sizeof(record), ids.data(),
				static_cast<uint32_t>(ids.size() * sizeof(uint32_t)));
		}

		// --- resources ------------------------------------------------------------------------
		for (const Json &entry : root["resources"].Items())
		{
			ResourceInfoRecord record = {};
			record.resource_id = entry["id"].AsUInt32();
			record.kind = static_cast<ResourceKind>(entry["kind"].AsUInt32());
			record.format = entry["format"].AsUInt32();
			record.width = entry["width"].AsUInt32();
			record.height = entry["height"].AsUInt32();
			record.depth_or_layers = entry["depth_or_layers"].AsUInt32();
			record.mip_levels = entry["mip_levels"].AsUInt32();
			record.samples = entry["samples"].AsUInt32();
			record.usage_flags = entry["usage_flags"].AsUInt32();
			record.buffer_size = entry["buffer_size"].AsUInt();
			record.created_frame = entry["created_frame"].AsUInt();
			record.created_event = entry["created_event"].AsUInt32();

			Apply(model, RecordType::resource_info, &record, sizeof(record));

			const std::string name = entry["name"].AsString();
			if (!name.empty())
			{
				ResourceNamedRecord named = {};
				named.resource_id = record.resource_id;
				named.name_length = static_cast<uint32_t>(name.size());
				Apply(model, RecordType::resource_named, &named, sizeof(named),
					reinterpret_cast<const uint8_t *>(name.data()), named.name_length);
			}
		}

		// --- the frame itself -----------------------------------------------------------------
		std::vector<uint8_t> event_bytes;
		if (!ReadFile(directory / "events.bin", event_bytes))
		{
			error = "events.bin is missing";
			return false;
		}

		const uint32_t event_count = static_cast<uint32_t>(event_bytes.size() / sizeof(FrameEvent));
		const uint64_t frame_index = root["frame"]["index"].AsUInt();

		FrameBeginRecord begin = {};
		begin.frame_index = frame_index;
		Apply(model, RecordType::frame_begin, &begin, sizeof(begin));

		constexpr uint32_t kChunk = 4096;
		for (uint32_t offset = 0; offset < event_count; offset += kChunk)
		{
			const uint32_t count = (std::min)(kChunk, event_count - offset);

			FrameEventsRecord record = {};
			record.frame_index = frame_index;
			record.event_count = count;
			record.first_index = offset;

			Apply(model, RecordType::frame_events, &record, sizeof(record),
				event_bytes.data() + static_cast<size_t>(offset) * sizeof(FrameEvent),
				count * static_cast<uint32_t>(sizeof(FrameEvent)));
		}

		FrameEndRecord end = {};
		end.frame_index = frame_index;
		end.draw_count = root["frame"]["draw_count"].AsUInt32();
		end.dispatch_count = root["frame"]["dispatch_count"].AsUInt32();
		end.event_count = event_count;
		end.dropped_events = root["frame"]["dropped_events"].AsUInt32();
		end.cpu_frame_ms = static_cast<float>(root["frame"]["cpu_frame_ms"].AsNumber());
		end.addon_cpu_ms = static_cast<float>(root["frame"]["addon_cpu_ms"].AsNumber());
		Apply(model, RecordType::frame_end, &end, sizeof(end));

		// --- deep capture side files -----------------------------------------------------------
		// Replayed as the records they were, so an opened deep capture goes through the same code
		// in SessionModel as a live one. Absent files are the normal case, not an error.
		std::vector<uint8_t> draw_bytes;
		if (ReadFile(directory / "drawstates.bin", draw_bytes))
		{
			size_t offset = 0;
			while (offset + sizeof(DrawStateRecord) <= draw_bytes.size())
			{
				DrawStateRecord record = {};
				std::memcpy(&record, draw_bytes.data() + offset, sizeof(record));
				offset += sizeof(record);

				const size_t bindings_bytes = static_cast<size_t>(record.binding_count) * sizeof(DrawBinding);
				if (offset + bindings_bytes > draw_bytes.size())
					break;   // truncated file: keep what was read rather than refusing the capture

				record.frame_index = frame_index;
				Apply(model, RecordType::draw_state, &record, sizeof(record),
					draw_bytes.data() + offset, static_cast<uint32_t>(bindings_bytes));
				offset += bindings_bytes;
			}
		}

		std::vector<uint8_t> barrier_bytes;
		if (ReadFile(directory / "barriers.bin", barrier_bytes))
		{
			size_t offset = 0;
			while (offset + sizeof(BarrierSetRecord) <= barrier_bytes.size())
			{
				BarrierSetRecord record = {};
				std::memcpy(&record, barrier_bytes.data() + offset, sizeof(record));
				offset += sizeof(record);

				const size_t entry_bytes = static_cast<size_t>(record.count) * sizeof(BarrierEntry);
				if (offset + entry_bytes > barrier_bytes.size())
					break;

				record.frame_index = frame_index;
				Apply(model, RecordType::barrier_set, &record, sizeof(record),
					barrier_bytes.data() + offset, static_cast<uint32_t>(entry_bytes));
				offset += entry_bytes;
			}
		}

		// --- timings ---------------------------------------------------------------------------
		const Json &timings = root["timings"];
		if (timings.IsObject() && timings.Has("by_event"))
		{
			std::vector<TimingResult> results;
			bool with_starts = false;
			for (const Json &entry : timings["by_event"].Items())
			{
				TimingResult result = {};
				result.event_index = entry["event"].AsUInt32();
				result.gpu_ticks = entry["ticks"].AsUInt();
				if (entry.Has("start"))
				{
					result.gpu_start = entry["start"].AsUInt();
					with_starts = true;
				}
				results.push_back(result);
			}

			TimingResultsRecord record = {};
			record.frame_index = timings["frame_index"].AsUInt();
			record.count = static_cast<uint32_t>(results.size());
			// A capture saved before starts existed is replayed in the old layout, so the
			// timeline knows the positions are not measured rather than reading zeros as such.
			// The first sixteen bytes of TimingResult are that layout.
			const uint32_t stride = with_starts ? static_cast<uint32_t>(sizeof(TimingResult))
			                                    : kTimingResultSizeV1;
			std::vector<uint8_t> blob(results.size() * stride);
			for (size_t i = 0; i < results.size(); ++i)
				std::memcpy(blob.data() + i * stride, &results[i], stride);
			record.result_size = stride;
			Apply(model, RecordType::timing_results, &record, sizeof(record), blob.data(),
				static_cast<uint32_t>(blob.size()));
		}

		return true;
	}

	std::vector<CaptureInfo> CaptureArchive::List(const std::filesystem::path &root)
	{
		std::vector<CaptureInfo> captures;

		std::error_code code;
		if (!std::filesystem::exists(root, code))
			return captures;

		for (const auto &entry : std::filesystem::directory_iterator(root, code))
		{
			if (!entry.is_directory())
				continue;

			CaptureInfo info;
			if (ReadInfo(entry.path(), info))
				captures.push_back(std::move(info));
		}

		// Newest first: the string timestamps sort the right way round.
		std::sort(captures.begin(), captures.end(),
			[](const CaptureInfo &a, const CaptureInfo &b) { return a.created > b.created; });
		return captures;
	}

	CaptureDiff CompareCaptures(const SessionModel &a, const SessionModel &b)
	{
		CaptureDiff diff;

		diff.draw_delta = static_cast<int32_t>(b.LastFrame().draw_count) -
			static_cast<int32_t>(a.LastFrame().draw_count);
		diff.dispatch_delta = static_cast<int32_t>(b.LastFrame().dispatch_count) -
			static_cast<int32_t>(a.LastFrame().dispatch_count);
		diff.event_delta = static_cast<int32_t>(b.LastFrame().events.size()) -
			static_cast<int32_t>(a.LastFrame().events.size());
		diff.resource_delta = static_cast<int32_t>(b.Resources().size()) -
			static_cast<int32_t>(a.Resources().size());

		// Shaders are compared by signature, not by id: ids are local to a session.
		std::unordered_set<std::string> signatures_a;
		std::unordered_set<std::string> signatures_b;
		for (const ShaderInfo &shader : a.Shaders())
			if (shader.id != 0)
				signatures_a.insert(shader.signature.ToHex());
		for (const ShaderInfo &shader : b.Shaders())
			if (shader.id != 0)
				signatures_b.insert(shader.signature.ToHex());

		for (const std::string &signature : signatures_a)
			if (signatures_b.count(signature) == 0)
				diff.only_in_a.push_back(signature.substr(0, 8));
		for (const std::string &signature : signatures_b)
			if (signatures_a.count(signature) == 0)
				diff.only_in_b.push_back(signature.substr(0, 8));

		std::sort(diff.only_in_a.begin(), diff.only_in_a.end());
		std::sort(diff.only_in_b.begin(), diff.only_in_b.end());

		diff.shader_delta = static_cast<int32_t>(signatures_b.size()) -
			static_cast<int32_t>(signatures_a.size());

		if (a.Timings().IsValid() && b.Timings().IsValid())
			diff.gpu_delta_ms = b.Timings().Milliseconds(b.Timings().total_ticks) -
				a.Timings().Milliseconds(a.Timings().total_ticks);
		else
			diff.notes.push_back("GPU timings are missing from at least one capture, so the time "
			                     "difference is not shown.");

		if (a.HasSession() && b.HasSession() &&
		    std::string(a.Session().process_name) != std::string(b.Session().process_name))
			diff.notes.push_back("These two captures come from different processes.");

		return diff;
	}
}
