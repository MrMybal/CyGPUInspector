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
#include <deque>
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

		// From one rebuild to the next (PassHistory): how many rebuilds in a row had this pass, and
		// the share of the recent ones that gave it the name it has now. 0 until tracked.
		uint32_t stable_samples = 0;
		float agreement = 0.0f;

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

	class FrameGraph;

	// What successive rebuilds of the graph said about each pass. A pass is followed by what it is —
	// its kind, its shaders, the formats and sizes of its targets — not by its position in the
	// frame, which changes whenever the game adds or removes a pass before it.
	//
	// It makes the names steadier and more honest. A name one rebuild gives and the next takes back
	// yields to the name most of the recent rebuilds agree on; a pass given the same name across the
	// last rebuilds gains a little confidence, never above 0.95; and a name the user gave follows
	// the pass wherever it moves in the frame.
	class PassHistory
	{
	public:
		void Clear();
		// The identity of a pass across rebuilds. The model lock is the caller's.
		static std::string KeyOf(const GraphPass &pass, const SessionModel &model);
		// A name given by the user to the pass with that key; empty removes it.
		void SetUserName(const std::string &key, const std::string &name);
		// Updates the history with a new graph and steadies its names. The model lock is the caller's.
		void Apply(FrameGraph &graph, const SessionModel &model);
		uint64_t Samples() const { return m_samples; }

		static constexpr size_t kWindow = 30;          // rebuilds remembered per pass
		static constexpr uint32_t kForgetAfter = 600;  // rebuilds without a pass before it is forgotten

	private:
		struct Entry
		{
			std::deque<std::string> names;            // the heuristic name of each recent rebuild
			std::unordered_map<std::string, float> confidence;   // the last confidence of each name
			std::string user_name;
			uint32_t consecutive = 0;
			uint64_t last_sample = 0;
		};
		std::unordered_map<std::string, Entry> m_entries;
		uint64_t m_samples = 0;
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
		// PassHistory rewrites names, origins and confidences; nothing else touches a built graph.
		std::vector<GraphPass> &MutablePasses() { return m_passes; }

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
