// CyGPUInspectorDatabase — what people and models say about a shader.
//
// Tags, a human readable name and annotations, all keyed by signature so they survive a restart of
// the game, a rebuild of the game, and even a move to another game.
//
// The central rule of this file: an annotation always carries **where it came from**. A sentence
// written by a person, a classification derived by a heuristic and a paragraph produced by a model
// are three different things and are never stored, displayed or exported as the same thing.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

#include "ShaderStore.hpp"

#include <CyGPUInspectorCore/Sha256.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace cygi
{
	// The vocabulary of section 39 of the brief.
	enum class ShaderTag : uint32_t
	{
		depth = 0,
		gbuffer,
		lighting,
		shadow,
		global_illumination,
		reflection,
		screen_space_reflection,
		ambient_occlusion,
		fog,
		volumetric,
		post_process,
		bloom,
		tonemap,
		upscale,
		ui,
		particles,
		video,
		unknown,
		count,
	};
	const char *ShaderTagName(ShaderTag tag);
	bool ShaderTagFromName(const std::string &name, ShaderTag &out);

	enum class AnnotationSource : uint32_t
	{
		user = 0,        // a person wrote this
		heuristic,       // the tool derived it from the data
		ai,              // a model produced it; never presented as a fact
	};
	const char *AnnotationSourceName(AnnotationSource source);

	enum class AnnotationKind : uint32_t
	{
		note = 0,
		classification,   // "this is probably a tonemapping pass"
		cleanup,          // renamed / commented HLSL
		explanation,      // what the shader does, in prose
	};
	const char *AnnotationKindName(AnnotationKind kind);

	struct Annotation
	{
		AnnotationSource source = AnnotationSource::user;
		AnnotationKind kind = AnnotationKind::note;
		std::string text;
		std::string author;          // model name for AI, empty for a person
		std::string prompt_version;  // so a result can be regenerated later (section 58)
		std::string created;
		float confidence = 0.0f;     // only meaningful for heuristic and AI sources

		// "AI (<model name>), classification, confidence 92%"
		std::string Describe() const;
	};

	struct ShaderNotes
	{
		std::string user_name;                 // what a person decided to call this shader
		std::vector<ShaderTag> tags;
		std::vector<Annotation> annotations;

		bool HasTag(ShaderTag tag) const;
		void SetTag(ShaderTag tag, bool enabled);
		std::string TagList() const;           // "Bloom, Post Process"
		bool IsEmpty() const;
	};

	// Both return false when there is simply nothing stored, which is not an error.
	bool LoadShaderNotes(const ShaderStore &store, const Sha256Digest &signature, ShaderNotes &out);
	bool SaveShaderNotes(ShaderStore &store, const Sha256Digest &signature, const ShaderNotes &notes);
}
