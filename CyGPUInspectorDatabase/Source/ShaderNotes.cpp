// CyGPUInspectorDatabase — tags and annotations.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDatabase/ShaderNotes.hpp"

#include <CyGPUInspectorCore/Json.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <cstdio>

namespace cygi
{
	namespace
	{
		const char *const kTagNames[] = {
			"Depth", "GBuffer", "Lighting", "Shadow", "GI", "Reflection", "SSR", "AO", "Fog",
			"Volumetric", "Post Process", "Bloom", "Tonemap", "Upscale", "UI", "Particles", "Video",
			"Unknown",
		};
		static_assert(sizeof(kTagNames) / sizeof(kTagNames[0]) == static_cast<size_t>(ShaderTag::count),
			"the tag names must match the enumeration");

		std::string NowString()
		{
			SYSTEMTIME now = {};
			GetSystemTime(&now);

			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02uZ", now.wYear, now.wMonth,
				now.wDay, now.wHour, now.wMinute, now.wSecond);
			return buffer;
		}
	}

	const char *ShaderTagName(ShaderTag tag)
	{
		const size_t index = static_cast<size_t>(tag);
		return index < static_cast<size_t>(ShaderTag::count) ? kTagNames[index] : "?";
	}

	bool ShaderTagFromName(const std::string &name, ShaderTag &out)
	{
		for (size_t i = 0; i < static_cast<size_t>(ShaderTag::count); ++i)
		{
			if (name == kTagNames[i])
			{
				out = static_cast<ShaderTag>(i);
				return true;
			}
		}
		return false;
	}

	const char *AnnotationSourceName(AnnotationSource source)
	{
		switch (source)
		{
		case AnnotationSource::heuristic: return "derived";
		case AnnotationSource::ai: return "AI";
		case AnnotationSource::user:
		default: return "user";
		}
	}

	const char *AnnotationKindName(AnnotationKind kind)
	{
		switch (kind)
		{
		case AnnotationKind::classification: return "classification";
		case AnnotationKind::cleanup: return "cleanup";
		case AnnotationKind::explanation: return "explanation";
		case AnnotationKind::note:
		default: return "note";
		}
	}

	std::string Annotation::Describe() const
	{
		std::string text_out = AnnotationSourceName(source);
		if (source == AnnotationSource::ai && !author.empty())
			text_out += " (" + author + ")";
		text_out += ", ";
		text_out += AnnotationKindName(kind);

		if (source != AnnotationSource::user && confidence > 0.0f)
		{
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), ", confidence %.0f%%", confidence * 100.0f);
			text_out += buffer;
		}
		return text_out;
	}

	bool ShaderNotes::HasTag(ShaderTag tag) const
	{
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}

	void ShaderNotes::SetTag(ShaderTag tag, bool enabled)
	{
		const auto it = std::find(tags.begin(), tags.end(), tag);
		if (enabled && it == tags.end())
			tags.push_back(tag);
		else if (!enabled && it != tags.end())
			tags.erase(it);
	}

	std::string ShaderNotes::TagList() const
	{
		std::string text;
		for (ShaderTag tag : tags)
		{
			if (!text.empty())
				text += ", ";
			text += ShaderTagName(tag);
		}
		return text;
	}

	bool ShaderNotes::IsEmpty() const
	{
		return user_name.empty() && tags.empty() && annotations.empty();
	}

	bool LoadShaderNotes(const ShaderStore &store, const Sha256Digest &signature, ShaderNotes &out)
	{
		out = ShaderNotes();

		std::string text;
		if (!store.LoadText(signature, "notes.json", text) || text.empty())
			return false;

		Json root;
		if (!Json::Parse(text, root) || !root.IsObject())
			return false;

		out.user_name = root["name"].AsString();

		for (const Json &tag : root["tags"].Items())
		{
			ShaderTag parsed = ShaderTag::unknown;
			if (ShaderTagFromName(tag.AsString(), parsed))
				out.tags.push_back(parsed);
		}

		for (const Json &entry : root["annotations"].Items())
		{
			Annotation annotation;
			annotation.text = entry["text"].AsString();
			annotation.author = entry["author"].AsString();
			annotation.prompt_version = entry["prompt_version"].AsString();
			annotation.created = entry["created"].AsString();
			annotation.confidence = static_cast<float>(entry["confidence"].AsNumber());

			const std::string source = entry["source"].AsString();
			annotation.source = source == "AI" ? AnnotationSource::ai
				: source == "derived" ? AnnotationSource::heuristic : AnnotationSource::user;

			const std::string kind = entry["kind"].AsString();
			annotation.kind = kind == "classification" ? AnnotationKind::classification
				: kind == "cleanup" ? AnnotationKind::cleanup
				: kind == "explanation" ? AnnotationKind::explanation : AnnotationKind::note;

			out.annotations.push_back(std::move(annotation));
		}
		return true;
	}

	bool SaveShaderNotes(ShaderStore &store, const Sha256Digest &signature, const ShaderNotes &notes)
	{
		Json root = Json::Object();
		root["signature"] = Json(signature.ToHex());
		root["name"] = Json(notes.user_name);
		root["updated"] = Json(NowString());

		Json tags = Json::Array();
		for (ShaderTag tag : notes.tags)
			tags.Push(Json(std::string(ShaderTagName(tag))));
		root["tags"] = tags;

		Json annotations = Json::Array();
		for (const Annotation &annotation : notes.annotations)
		{
			Json entry = Json::Object();
			entry["source"] = Json(std::string(AnnotationSourceName(annotation.source)));
			entry["kind"] = Json(std::string(AnnotationKindName(annotation.kind)));
			entry["text"] = Json(annotation.text);
			entry["author"] = Json(annotation.author);
			entry["prompt_version"] = Json(annotation.prompt_version);
			entry["created"] = Json(annotation.created.empty() ? NowString() : annotation.created);
			entry["confidence"] = Json(static_cast<double>(annotation.confidence));
			annotations.Push(entry);
		}
		root["annotations"] = annotations;

		return store.SaveText(signature, "notes.json", root.Write(2));
	}
}
