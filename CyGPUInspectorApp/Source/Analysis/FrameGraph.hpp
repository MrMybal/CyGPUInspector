// CyGPUInspectorApp — the frame graph, reconstructed from dependencies.
//
// No game tells us what its passes are: the ReShade add-on API exposes neither PIX events nor
// ID3DUserDefinedAnnotation (see Docs/Research/ReShadeAddonAPI.md). So the graph is derived, in
// three steps, from the event stream alone:
//
//   1. clustering      consecutive commands that share a target become one pass
//   2. dependencies    a pass that reads what another wrote gets an edge from it
//   3. classification  invariants give a probable name, always with its confidence and origin
//
// Everything here runs on the UI thread with the session model locked, over the events of one
// frame. It is pure analysis: it never talks to the game.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "Session/SessionModel.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cygi
{
	// Where a pass name comes from. Never hidden from the user: an inference is not a fact.
	enum class PassNameOrigin : uint8_t
	{
		unknown = 0,
		heuristic,
		user,
		ai,
	};
	const char *PassNameOriginName(PassNameOrigin origin);

	enum class PassKind : uint8_t
	{
		unknown = 0,
		graphics,
		compute,
		transfer,   // copies, resolves, clears, mip generation
	};

	struct GraphPass
	{
		uint32_t id = 0;
		std::string name;
		PassNameOrigin origin = PassNameOrigin::unknown;
		float confidence = 0.0f;
		PassKind kind = PassKind::unknown;

		uint32_t first_event = 0;
		uint32_t last_event = 0;
		uint32_t event_count = 0;
		uint32_t draw_count = 0;
		uint32_t dispatch_count = 0;

		std::vector<uint32_t> render_targets;   // resource ids, in slot order
		uint32_t depth_target = 0;
		std::vector<uint32_t> reads;            // resource ids this pass consumed
		std::vector<uint32_t> shaders;          // distinct shader ids seen

		bool WritesResource(uint32_t resource_id) const;
	};

	// "pass A wrote resource R, pass B read it".
	struct GraphEdge
	{
		uint32_t from_pass = 0;
		uint32_t to_pass = 0;
		uint32_t resource_id = 0;
		uint32_t event_index = 0;   // the command that created the dependency
	};

	// Everything that happened to one resource during the frame (section 7 of the brief).
	struct ResourceFlow
	{
		uint32_t resource_id = 0;
		std::vector<uint32_t> written_by;    // pass ids
		std::vector<uint32_t> read_by;       // pass ids
		std::vector<uint32_t> copied_from;   // resource ids
		std::vector<uint32_t> copied_to;
		std::vector<uint32_t> resolved_from;
		std::vector<uint32_t> resolved_to;
		uint32_t first_write_event = 0;
		uint32_t last_write_event = 0;
		uint32_t last_read_event = 0;
		bool cleared = false;
	};

	class FrameGraph
	{
	public:
		// The caller must hold the model lock. Rebuilds from scratch: a frame is small enough.
		void Build(const SessionModel &model);
		// The same, for a frame other than the last one: the continuous timeline cuts each frame
		// it keeps into passes with that frame's own events.
		void Build(const SessionModel &model, const FrameInfo &frame);
		void Clear();

		bool IsValid() const { return m_valid; }
		uint64_t FrameIndex() const { return m_frame_index; }

		const std::vector<GraphPass> &Passes() const { return m_passes; }
		const std::vector<GraphEdge> &Edges() const { return m_edges; }

		const GraphPass *PassById(uint32_t pass_id) const;
		const GraphPass *PassOfEvent(uint32_t event_index) const;
		const ResourceFlow *FlowOf(uint32_t resource_id) const;

		// Lets the user override a derived name; the origin becomes `user`.
		void RenamePass(uint32_t pass_id, const std::string &name);

		double BuildMilliseconds() const { return m_build_milliseconds; }

	private:
		void Cluster(const SessionModel &model, const FrameInfo &frame);
		void BuildDependencies();
		void Classify(const SessionModel &model);

		bool m_valid = false;
		uint64_t m_frame_index = 0;
		double m_build_milliseconds = 0.0;

		std::vector<GraphPass> m_passes;          // indexed by id - 1
		std::vector<GraphEdge> m_edges;
		std::unordered_map<uint32_t, ResourceFlow> m_flows;
		std::unordered_map<uint32_t, std::string> m_user_names;   // survives a rebuild
	};
}
