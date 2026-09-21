// CyGPUInspectorApp — the frame graph, reconstructed from dependencies.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "FrameGraph.hpp"

#include <CyGPUInspectorCore/Format.hpp>

#include <algorithm>
#include <chrono>

namespace cygi
{
	namespace
	{
		bool IsDraw(EventKind kind)
		{
			return kind == EventKind::draw || kind == EventKind::draw_indexed ||
			       kind == EventKind::draw_indirect;
		}

		bool IsDispatch(EventKind kind)
		{
			return kind == EventKind::dispatch || kind == EventKind::dispatch_indirect ||
			       kind == EventKind::dispatch_mesh || kind == EventKind::dispatch_rays;
		}

		bool IsTransfer(EventKind kind)
		{
			switch (kind)
			{
			case EventKind::copy_resource:
			case EventKind::copy_buffer_region:
			case EventKind::copy_texture_region:
			case EventKind::copy_buffer_to_texture:
			case EventKind::copy_texture_to_buffer:
			case EventKind::resolve:
			case EventKind::clear_render_target:
			case EventKind::clear_depth_stencil:
			case EventKind::clear_unordered_access:
			case EventKind::generate_mipmaps:
				return true;
			default:
				return false;
			}
		}

		void AddUnique(std::vector<uint32_t> &list, uint32_t value)
		{
			if (value == 0)
				return;
			if (std::find(list.begin(), list.end(), value) == list.end())
				list.push_back(value);
		}

		// The key that decides whether a command belongs to the pass being built.
		struct ClusterKey
		{
			PassKind kind = PassKind::unknown;
			uint32_t primary = 0;
			uint32_t secondary = 0;
			uint32_t pipeline = 0;

			bool operator==(const ClusterKey &other) const
			{
				return kind == other.kind && primary == other.primary && secondary == other.secondary &&
				       pipeline == other.pipeline;
			}
			bool operator!=(const ClusterKey &other) const { return !(*this == other); }
		};

		ClusterKey KeyOf(const FrameEvent &event)
		{
			ClusterKey key;
			if (IsDraw(event.kind))
			{
				// Draws group by what they write: that is exactly what a rendering pass is.
				key.kind = PassKind::graphics;
				key.primary = event.primary_resource;
				key.secondary = event.secondary_resource;
			}
			else if (IsDispatch(event.kind))
			{
				// A dispatch rarely tells us what it writes without descriptor tracking, and the
				// pipeline is deliberately *not* part of the key. Grouping by pipeline looked
				// right and was wrong on a real engine: a post-processing chain is twenty
				// different compute pipelines in a row, which came out as twenty passes of one
				// command each. What actually separates two compute passes is the barrier between
				// them, and that is handled in the loop below.
				key.kind = PassKind::compute;
				key.primary = event.primary_resource;
			}
			else
			{
				key.kind = PassKind::transfer;
				key.primary = event.primary_resource;
				key.secondary = event.secondary_resource;
			}
			return key;
		}
	}

	const char *PassNameOriginName(PassNameOrigin origin)
	{
		switch (origin)
		{
		case PassNameOrigin::heuristic: return "derived";
		case PassNameOrigin::user: return "user";
		case PassNameOrigin::ai: return "AI";
		case PassNameOrigin::unknown:
		default: return "unnamed";
		}
	}

	bool GraphPass::WritesResource(uint32_t resource_id) const
	{
		if (resource_id == 0)
			return false;
		if (depth_target == resource_id)
			return true;
		return std::find(render_targets.begin(), render_targets.end(), resource_id) != render_targets.end();
	}

	void FrameGraph::Clear()
	{
		m_valid = false;
		m_frame_index = 0;
		m_passes.clear();
		m_edges.clear();
		m_flows.clear();
	}

	void FrameGraph::Build(const SessionModel &model)
	{
		Build(model, model.LastFrame());
	}

	void FrameGraph::Build(const SessionModel &model, const FrameInfo &frame)
	{
		const auto start = std::chrono::steady_clock::now();

		m_passes.clear();
		m_edges.clear();
		m_flows.clear();
		m_frame_index = frame.index;

		Cluster(model, frame);
		BuildDependencies();
		Classify(model);

		m_valid = !m_passes.empty();
		m_build_milliseconds =
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}

	void FrameGraph::Cluster(const SessionModel &model, const FrameInfo &frame)
	{
		const std::vector<FrameEvent> &events = frame.events;
		if (events.empty())
			return;

		ClusterKey current_key;
		GraphPass *pass = nullptr;

		// The last seen render target binding: draws only carry slot 0, so the full set comes
		// from the binding events the add-on records.
		std::vector<uint32_t> bound_targets;
		uint32_t bound_depth = 0;
		bool barrier_since_last = false;

		for (const FrameEvent &event : events)
		{
			if (event.kind == EventKind::bind_render_targets || event.kind == EventKind::begin_render_pass)
			{
				bound_targets.clear();
				AddUnique(bound_targets, event.primary_resource);
				AddUnique(bound_targets, event.b);
				AddUnique(bound_targets, event.c);
				AddUnique(bound_targets, event.d);
				bound_depth = event.secondary_resource;
				continue;
			}
			if (event.kind == EventKind::barrier)
			{
				// Not a command of the pass, but the thing that ends one: a barrier between two
				// dispatches is an engine saying the second reads what the first wrote.
				barrier_since_last = true;
				continue;
			}
			if (event.kind == EventKind::end_render_pass || event.kind == EventKind::present ||
			    event.kind == EventKind::bind_pipeline)
				continue;

			const ClusterKey key = KeyOf(event);
			const bool split_on_barrier = barrier_since_last && pass != nullptr &&
				key.kind == PassKind::compute && current_key.kind == PassKind::compute;
			barrier_since_last = false;

			if (pass == nullptr || key != current_key || split_on_barrier)
			{
				m_passes.emplace_back();
				pass = &m_passes.back();
				pass->id = static_cast<uint32_t>(m_passes.size());
				pass->kind = key.kind;
				pass->first_event = event.index;
				current_key = key;

				if (key.kind == PassKind::graphics)
				{
					// Trust the binding when it agrees with the draw, otherwise trust the draw.
					if (!bound_targets.empty() && bound_targets.front() == event.primary_resource)
						pass->render_targets = bound_targets;
					else
						AddUnique(pass->render_targets, event.primary_resource);
					pass->depth_target = event.secondary_resource != 0 ? event.secondary_resource : bound_depth;
				}
				else if (key.kind == PassKind::compute)
				{
					AddUnique(pass->render_targets, event.primary_resource);
				}
				else
				{
					AddUnique(pass->render_targets, event.primary_resource);
					AddUnique(pass->reads, event.secondary_resource);
				}
			}

			pass->last_event = event.index;
			++pass->event_count;

			if (IsDraw(event.kind))
				++pass->draw_count;
			else if (IsDispatch(event.kind))
				++pass->dispatch_count;
			else if (IsTransfer(event.kind))
				AddUnique(pass->reads, event.secondary_resource);

			if (event.pipeline_id != 0)
				for (uint32_t shader_id : model.ShadersOfPipeline(event.pipeline_id))
					AddUnique(pass->shaders, shader_id);

			// Record what happened to the resources, for the dependency graph.
			if (event.primary_resource != 0)
			{
				ResourceFlow &flow = m_flows[event.primary_resource];
				flow.resource_id = event.primary_resource;
				AddUnique(flow.written_by, pass->id);
				if (flow.first_write_event == 0)
					flow.first_write_event = event.index;
				flow.last_write_event = event.index;

				if (event.kind == EventKind::clear_render_target ||
				    event.kind == EventKind::clear_depth_stencil ||
				    event.kind == EventKind::clear_unordered_access)
					flow.cleared = true;
				if (event.kind == EventKind::copy_resource || event.kind == EventKind::copy_texture_region)
					AddUnique(flow.copied_from, event.secondary_resource);
				if (event.kind == EventKind::resolve)
					AddUnique(flow.resolved_from, event.secondary_resource);
			}

			if (event.secondary_resource != 0)
			{
				ResourceFlow &flow = m_flows[event.secondary_resource];
				flow.resource_id = event.secondary_resource;
				AddUnique(flow.read_by, pass->id);
				flow.last_read_event = event.index;

				// For a draw the secondary resource is the depth target, which a depth prepass
				// very much writes. The API does not tell us whether depth writes are enabled, so
				// it counts as both: that is what makes a prepass appear as a producer.
				if (IsDraw(event.kind) || IsDispatch(event.kind))
				{
					AddUnique(flow.written_by, pass->id);
					if (flow.first_write_event == 0)
						flow.first_write_event = event.index;
					flow.last_write_event = event.index;
				}

				if (event.kind == EventKind::copy_resource || event.kind == EventKind::copy_texture_region)
					AddUnique(flow.copied_to, event.primary_resource);
				if (event.kind == EventKind::resolve)
					AddUnique(flow.resolved_to, event.primary_resource);

				AddUnique(pass->reads, event.secondary_resource);
			}
		}
	}

	void FrameGraph::BuildDependencies()
	{
		// A pass depends on the last pass that wrote something it reads. Only the latest writer
		// matters: an earlier one is already shadowed by it.
		for (const GraphPass &consumer : m_passes)
		{
			for (uint32_t resource_id : consumer.reads)
			{
				const auto it = m_flows.find(resource_id);
				if (it == m_flows.end())
					continue;

				uint32_t producer = 0;
				for (uint32_t candidate : it->second.written_by)
				{
					if (candidate >= consumer.id)
						break;
					producer = candidate;
				}
				if (producer == 0)
					continue;

				GraphEdge edge;
				edge.from_pass = producer;
				edge.to_pass = consumer.id;
				edge.resource_id = resource_id;
				edge.event_index = consumer.first_event;
				m_edges.push_back(edge);
			}
		}
	}

	void FrameGraph::Classify(const SessionModel &model)
	{
		const uint32_t pass_count = static_cast<uint32_t>(m_passes.size());
		uint32_t unknown_index = 0;

		// Remember the width of the previous colour pass to spot a downsample chain.
		uint32_t previous_width = 0;
		bool previous_was_hdr = false;

		// The size of what is presented. A depth only pass at that size is a prepass on the camera;
		// one at another size is drawing depth for something else, which in practice means a shadow
		// map. That is a better question to ask than "is it early in the frame", which called every
		// shadow cascade of a real game a depth prepass.
		uint32_t present_width = 0;
		uint32_t present_height = 0;
		for (const GraphPass &pass : m_passes)
		{
			for (const uint32_t resource_id : pass.render_targets)
			{
				const ResourceInfo *resource = model.ResourceById(resource_id);
				if (resource != nullptr && (resource->usage_flags & kUsageBackBuffer) != 0)
				{
					present_width = resource->width;
					present_height = resource->height;
					break;
				}
			}
			if (present_width != 0)
				break;
		}

		for (uint32_t index = 0; index < pass_count; ++index)
		{
			GraphPass &pass = m_passes[index];

			const auto user = m_user_names.find(pass.id);
			if (user != m_user_names.end())
			{
				pass.name = user->second;
				pass.origin = PassNameOrigin::user;
				pass.confidence = 1.0f;
				continue;
			}

			const ResourceInfo *target = nullptr;
			for (uint32_t resource_id : pass.render_targets)
			{
				target = model.ResourceById(resource_id);
				if (target != nullptr)
					break;
			}
			const ResourceInfo *depth = model.ResourceById(pass.depth_target);

			const bool early = index < pass_count / 3;
			const bool late = index + 1 >= pass_count - (pass_count / 6);
			const bool writes_backbuffer = target != nullptr &&
				(target->usage_flags & kUsageBackBuffer) != 0;
			const bool hdr = target != nullptr && FormatIsFloat(target->format);
			const uint32_t width = target != nullptr ? target->width : 0;

			pass.origin = PassNameOrigin::heuristic;

			if (pass.kind == PassKind::transfer)
			{
				pass.name = "Copy / Clear";
				pass.confidence = 0.9f;
			}
			else if (pass.draw_count > 0 && pass.render_targets.empty() && depth != nullptr)
			{
				// Depth only, no colour: a prepass or a shadow map. The camera's depth buffer is
				// the size of what is presented; a shadow map is some other size, and usually
				// square. When the presented size is unknown, position in the frame is all that
				// is left, and the confidence says so.
				const bool camera_sized = present_width != 0 &&
					depth->width == present_width && depth->height == present_height;
				if (present_width != 0)
				{
					pass.name = camera_sized ? "Depth Prepass" : "Shadow Map";
					pass.confidence = camera_sized ? 0.8f : 0.7f;
				}
				else
				{
					pass.name = early ? "Depth Prepass" : "Shadow Map";
					pass.confidence = 0.45f;
				}
			}
			else if (pass.render_targets.size() >= 2 && pass.depth_target != 0 && pass.draw_count > 10)
			{
				// Several colour targets and a depth buffer, written by many draws: a geometry
				// pass filling a G-buffer. Three or more targets is the classic shape and is
				// believed more than two, which some engines also use for other things.
				pass.name = "GBuffer";
				pass.confidence = pass.render_targets.size() >= 3 ? 0.85f : 0.6f;
			}
			else if (writes_backbuffer && pass.draw_count > 5 && late)
			{
				pass.name = "UI";
				pass.confidence = 0.7f;
			}
			else if (writes_backbuffer && pass.draw_count <= 5)
			{
				pass.name = "Tonemap / Present";
				pass.confidence = 0.6f;
			}
			else if (pass.kind == PassKind::compute && hdr && previous_was_hdr && previous_width != 0 &&
			         width != 0 && width <= previous_width / 2)
			{
				pass.name = "Bloom Downsample";
				pass.confidence = 0.55f;
			}
			else if (pass.kind == PassKind::compute && pass.reads.size() >= 2 && !early)
			{
				pass.name = "Compute Pass";
				pass.confidence = 0.3f;
			}
			else if (pass.kind == PassKind::compute)
			{
				pass.name = "Compute Pass";
				pass.confidence = 0.3f;
			}
			else
			{
				// Nothing recognised it. The number keeps it identifiable, and the size of what it
				// writes is added because that is a fact rather than a guess, and it is what makes
				// one unnamed pass distinguishable from another in a list. Still origin `unknown`
				// and confidence zero: this is not a claim about what the pass is (§40).
				pass.name = "Unknown Pass #" + std::to_string(++unknown_index);
				if (target != nullptr && target->width != 0)
					pass.name += " (" + std::to_string(target->width) + "x" +
						std::to_string(target->height) + ")";
				pass.origin = PassNameOrigin::unknown;
				pass.confidence = 0.0f;
			}

			if (width != 0)
			{
				previous_width = width;
				previous_was_hdr = hdr;
			}
		}
	}

	const GraphPass *FrameGraph::PassById(uint32_t pass_id) const
	{
		return (pass_id != 0 && pass_id <= m_passes.size()) ? &m_passes[pass_id - 1] : nullptr;
	}

	const GraphPass *FrameGraph::PassOfEvent(uint32_t event_index) const
	{
		for (const GraphPass &pass : m_passes)
			if (event_index >= pass.first_event && event_index <= pass.last_event)
				return &pass;
		return nullptr;
	}

	const ResourceFlow *FrameGraph::FlowOf(uint32_t resource_id) const
	{
		const auto it = m_flows.find(resource_id);
		return it != m_flows.end() ? &it->second : nullptr;
	}

	void FrameGraph::RenamePass(uint32_t pass_id, const std::string &name)
	{
		if (name.empty())
			m_user_names.erase(pass_id);
		else
			m_user_names[pass_id] = name;

		if (GraphPass *pass = const_cast<GraphPass *>(PassById(pass_id)))
		{
			if (name.empty())
			{
				pass->origin = PassNameOrigin::unknown;
			}
			else
			{
				pass->name = name;
				pass->origin = PassNameOrigin::user;
				pass->confidence = 1.0f;
			}
		}
	}
}
