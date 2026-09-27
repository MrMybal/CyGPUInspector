// CyGPUInspectorCore — the tools an AI agent can call, and what each one is for.
//
// One table for the two ways a model reaches CyGPUInspector: CyGPUInspectorMCP publishes it to an
// external agent, and the assistant built into CyGPUInspectorApp gives it to the provider it is
// connected to. Both end in the same handler in the standalone, so a tool cannot mean one thing
// to one of them and another thing to the other.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

namespace cygi
{
	struct McpToolDefinition
	{
		const char *name;
		const char *description;
		const char *arguments;   // JSON schema properties, or "" for none
	};

	// Section 49 of the brief. The descriptions matter: they are what an agent reads to decide
	// what to call, so they say what the data means and what it does not.
	inline constexpr McpToolDefinition kMcpTools[] = {
		{ "get_session", "What CyGPUInspector is currently looking at: the connected game or the open capture.", "" },
		{ "get_current_frame", "Counters of the frame currently held: draws, dispatches, events, CPU and GPU time.", "" },
		{ "get_game_profile", "What is remembered about this game across sessions: sessions, shaders known, the shaders new in this session (never seen in an earlier one), and the saved state of disabled and replaced shaders. Optional: limit (new shaders listed, default 100).",
		  "\"limit\":{\"type\":\"integer\"}" },

		{ "list_shaders", "Every shader seen. Optional: query (text filter), used_this_frame (bool), limit.",
		  "\"query\":{\"type\":\"string\"},\"used_this_frame\":{\"type\":\"boolean\"},\"limit\":{\"type\":\"integer\"}" },
		{ "search_shaders", "Shaders used in the current frame matching a text query on stage, format or signature.",
		  "\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}" },
		{ "find_shaders", "Shaders of the analysed frame found by what they do. query is a line of words, all of which must match: stage:cs (vs ps cs gs hs ds ms as), writes:rgba16f / writes:depth / writes:hdr / writes:#12 (a target their draws write: format, depth, any float format, or a resource id), reads:depth / reads:#7 (a resource bound for reading, needs a deep capture), ms>0.2, calls>1000, format:dxil, sm:6, tag:bloom, name:tonemap, used, disabled, replaced, new (new in this game's profile), or free text matched against the signature and the stage. Example: \"stage:cs writes:#12\". Optional: limit.",
		  "\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}" },
		{ "inspect_shader", "Everything known about one shader, including its reflected bindings when analysed.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "get_shader_disassembly", "The DXBC or DXIL disassembly of a shader. Starts the work if needed.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "get_shader_decompiled_hlsl", "HLSL reconstructions of a shader, one per backend, each with its validation verdict. These are reconstructions, never the original source.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "decompile_shader", "Runs every decompiler backend on a shader and returns all of their outputs.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "get_shader_draw_calls", "The indices of the draws and dispatches that ran a shader in the current frame.",
		  "\"shader_id\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\"}" },
		{ "get_shader_timing", "GPU time attributed to a shader. Needs a profiling tracking level.",
		  "\"shader_id\":{\"type\":\"integer\"}" },

		{ "list_resources", "Every resource seen. Optional: query, render_targets_only, limit.",
		  "\"query\":{\"type\":\"string\"},\"render_targets_only\":{\"type\":\"boolean\"},\"limit\":{\"type\":\"integer\"}" },
		{ "search_resources", "Resources matching a text query on kind, format or usage.",
		  "\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}" },
		{ "inspect_resource", "Everything known about one resource, including its lifetime and per frame access counts.",
		  "\"resource_id\":{\"type\":\"integer\"}" },
		{ "get_resource_writers", "The passes that wrote a resource in the analysed frame.",
		  "\"resource_id\":{\"type\":\"integer\"}" },
		{ "get_resource_readers", "The passes that read a resource in the analysed frame.",
		  "\"resource_id\":{\"type\":\"integer\"}" },

		{ "get_frame_graph", "The reconstructed passes and the dependencies between them. No game names its passes: every name is derived and carries its confidence.", "" },
		{ "get_passes", "The reconstructed passes alone, without the dependency edges.", "" },
		{ "inspect_draw", "One command of the frame: its pipeline, shaders, targets, counts and GPU time.",
		  "\"event_index\":{\"type\":\"integer\"}" },
		{ "inspect_dispatch", "Same as inspect_draw, for a compute dispatch.",
		  "\"event_index\":{\"type\":\"integer\"}" },

		{ "disable_shader", "Makes the game skip every draw that uses this shader. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "enable_shader", "Undoes disable_shader. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "highlight_shader", "Replaces a shader with a generated one that writes magenta. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
		{ "capture_frame", "Saves the current frame as a capture on disk. Needs Debug Control.", "" },
		{ "start_deep_capture", "Arms the expensive one shot capture in the game: every descriptor bound to every command, the pipeline state and the barriers, for a few frames. buffers (default true) also saves every texture the last captured frame wrote to, as PNG and DDS, in the capture's folder. Needs Debug Control.",
		  "\"frames\":{\"type\":\"integer\"},\"bindings\":{\"type\":\"boolean\"},\"barriers\":{\"type\":\"boolean\"},\"per_draw_timing\":{\"type\":\"boolean\"},\"buffers\":{\"type\":\"boolean\"}" },
		{ "get_capture_state", "Where the deep capture has got to, how much it recorded, the folder it was saved to and each saved buffer with its files.", "" },
		{ "list_captures", "The captures saved on disk: name, game, frame, draws, shaders, date. A name is what compare_captures takes.", "" },
		{ "compare_captures", "What changed between two frames (\"light off, light on: what changed?\"): a and b are capture names from list_captures, or \"live\" for the frame of the connected game. Returns the shaders that ran in only one of them, the shaders whose calls or GPU time changed, the resources present in only one, and the differences of counts and GPU time. Shader ids are those of each frame. Optional: limit (entries per list, default 50).",
		  "\"a\":{\"type\":\"string\"},\"b\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\"}" },
		{ "get_frame_timeline", "The passes of the analysed frame in order, each with where it starts on the GPU, how long it lasts, its share of the frame, and whether that length was measured or placed between two measurements. This is the runtime capture, which costs the game almost nothing. include_measurements adds every raw timestamp.",
		  "\"include_measurements\":{\"type\":\"boolean\"}" },
		{ "get_command_state", "What one command was given: every descriptor on every slot, the viewport, the scissor and the fixed function state of its pipeline. Needs a deep capture to have run.",
		  "\"event_index\":{\"type\":\"integer\"}" },

		{ "compile_shader", "Compiles HLSL for the profile of a shader without injecting it. Needs Shader Modification.",
		  "\"shader_id\":{\"type\":\"integer\"},\"hlsl\":{\"type\":\"string\"}" },
		{ "replace_shader", "Compiles HLSL and injects it in place of a shader, live. Needs Shader Modification.",
		  "\"shader_id\":{\"type\":\"integer\"},\"hlsl\":{\"type\":\"string\"}" },
		{ "restore_shader", "Puts the original shader back. Needs Debug Control.",
		  "\"shader_id\":{\"type\":\"integer\"}" },
	};
}
