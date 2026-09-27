// CyGPUInspectorApp — searching the shaders of a frame by what they do (section 46).
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "ShaderQuery.hpp"

#include <CyGPUInspectorCore/Format.hpp>
#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace cygi
{
	namespace
	{
		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		std::string WithoutUnderscores(std::string text)
		{
			text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
			return text;
		}

		bool StartsWith(const std::string &text, const std::string &prefix)
		{
			return text.compare(0, prefix.size(), prefix) == 0;
		}

		bool StageFromWord(const std::string &word, ShaderStage &out)
		{
			static const struct { const char *name; ShaderStage stage; } kStages[] = {
				{ "vs", ShaderStage::vertex }, { "vertex", ShaderStage::vertex },
				{ "ps", ShaderStage::pixel }, { "pixel", ShaderStage::pixel }, { "fragment", ShaderStage::pixel },
				{ "cs", ShaderStage::compute }, { "compute", ShaderStage::compute },
				{ "gs", ShaderStage::geometry }, { "geometry", ShaderStage::geometry },
				{ "hs", ShaderStage::hull }, { "hull", ShaderStage::hull },
				{ "ds", ShaderStage::domain }, { "domain", ShaderStage::domain },
				{ "ms", ShaderStage::mesh }, { "mesh", ShaderStage::mesh },
				{ "as", ShaderStage::amplification }, { "amplification", ShaderStage::amplification },
			};
			for (const auto &entry : kStages)
				if (word == entry.name)
				{
					out = entry.stage;
					return true;
				}
			return false;
		}

		// "ms>0.2" -> key "ms", comparison; false when the word is not a comparison.
		bool ParseComparison(const std::string &word, std::string &key, ShaderQuery::Comparison &out, bool &valid)
		{
			const size_t op = word.find_first_of("<>=");
			if (op == std::string::npos || op == 0)
				return false;
			key = word.substr(0, op);
			size_t value_start = op + 1;
			out = ShaderQuery::Comparison();
			out.active = true;
			if (word[op] == '=')
			{
				out.exact = true;
			}
			else
			{
				out.greater = word[op] == '>';
				if (value_start < word.size() && word[value_start] == '=')
				{
					out.or_equal = true;
					++value_start;
				}
			}
			const std::string number = word.substr(value_start);
			char *end = nullptr;
			out.value = std::strtod(number.c_str(), &end);
			valid = !number.empty() && end != nullptr && *end == '\0';
			return true;
		}

		// r16g16b16a16_float -> { "rgba16f", "rgba16float" }; r8g8b8a8_unorm -> { "rgba8", "rgba8unorm" }.
		std::vector<std::string> CompactAliases(const std::string &name)
		{
			const size_t underscore = name.find('_');
			const std::string channels_part = name.substr(0, underscore);
			std::string type = underscore == std::string::npos ? std::string() : WithoutUnderscores(name.substr(underscore + 1));

			std::string channels;
			std::vector<std::string> bits;
			for (size_t i = 0; i < channels_part.size();)
			{
				if (!std::isalpha(static_cast<unsigned char>(channels_part[i])))
					return {};
				channels += channels_part[i++];
				std::string count;
				while (i < channels_part.size() && std::isdigit(static_cast<unsigned char>(channels_part[i])))
					count += channels_part[i++];
				bits.push_back(count);
			}
			if (channels.empty())
				return {};
			const bool uniform = std::all_of(bits.begin(), bits.end(), [&](const std::string &b) { return b == bits[0]; });
			const std::string base = uniform ? channels + bits[0] : channels_part;

			std::vector<std::string> aliases = { base + type };
			if (type == "float")
				aliases.push_back(base + "f");
			else if (type == "unorm")
				aliases.push_back(base);
			else if (type == "sint")
				aliases.push_back(base + "i");
			else if (type == "uint")
				aliases.push_back(base + "ui");
			else if (type == "unormsrgb")
				aliases.push_back(base + "srgb");
			return aliases;
		}

		bool Contains(const std::vector<uint32_t> &list, uint32_t value)
		{
			return std::find(list.begin(), list.end(), value) != list.end();
		}

		// A pattern of writes: or reads: against one resource.
		bool ResourceMatches(const SessionModel &model, uint32_t resource_id, const std::string &pattern)
		{
			if (pattern.size() > 1 && pattern[0] == '#')
				return resource_id == static_cast<uint32_t>(std::strtoul(pattern.c_str() + 1, nullptr, 10));
			const ResourceInfo *resource = model.ResourceById(resource_id);
			if (resource == nullptr)
				return false;
			if (pattern == "depth")
				return FormatIsDepth(resource->format) || (resource->usage_flags & kUsageDepthStencil) != 0;
			return FormatMatches(resource->format, pattern);
		}
	}

	bool ShaderQuery::Comparison::Accepts(double measured) const
	{
		if (!active)
			return true;
		if (exact)
			return measured == value;
		if (or_equal && measured == value)
			return true;
		return greater ? measured > value : measured < value;
	}

	ShaderQuery ShaderQuery::Parse(const std::string &text)
	{
		ShaderQuery query;
		std::istringstream stream(text);
		std::string token;
		while (stream >> token)
		{
			const std::string word = Lower(token);

			if (word == "used" || word == "disabled" || word == "replaced" || word == "new")
			{
				(word == "used" ? query.used : word == "disabled" ? query.disabled
					: word == "replaced" ? query.replaced : query.new_only) = true;
				query.structured = true;
				continue;
			}

			std::string key;
			Comparison comparison;
			bool valid = false;
			if (word.find(':') == std::string::npos && ParseComparison(word, key, comparison, valid))
			{
				if ((key == "ms" || key == "calls" || key == "draws") && valid)
				{
					(key == "ms" ? query.gpu_ms : query.calls) = comparison;
					query.structured = true;
				}
				else
				{
					query.errors.push_back(token);
				}
				continue;
			}

			const size_t colon = word.find(':');
			if (colon == std::string::npos)
			{
				query.words.push_back(word);
				continue;
			}

			key = word.substr(0, colon);
			std::vector<std::string> values;
			std::string rest = word.substr(colon + 1);
			for (size_t start = 0; start <= rest.size();)
			{
				size_t end = rest.find(',', start);
				if (end == std::string::npos)
					end = rest.size();
				if (end > start)
					values.push_back(rest.substr(start, end - start));
				start = end + 1;
			}
			if (values.empty())
			{
				query.errors.push_back(token);
				continue;
			}

			query.structured = true;
			if (key == "stage")
			{
				for (const std::string &value : values)
				{
					ShaderStage stage;
					if (StageFromWord(value, stage))
						query.stages.push_back(stage);
					else
						query.errors.push_back(token);
				}
			}
			else if (key == "writes" || key == "write")
				query.writes.insert(query.writes.end(), values.begin(), values.end());
			else if (key == "reads" || key == "read")
				query.reads.insert(query.reads.end(), values.begin(), values.end());
			else if (key == "format")
			{
				if (values[0] == "dxbc" || values[0] == "dxil")
					query.format = values[0];
				else
					query.errors.push_back(token);
			}
			else if (key == "sm")
			{
				std::string model = values[0];
				std::replace(model.begin(), model.end(), '.', '_');
				query.shader_model = model;
			}
			else if (key == "tag")
				query.tags.insert(query.tags.end(), values.begin(), values.end());
			else if (key == "name")
				query.names.insert(query.names.end(), values.begin(), values.end());
			else
				query.errors.push_back(token);
		}
		return query;
	}

	bool FormatMatches(uint32_t format, const std::string &pattern_text)
	{
		const std::string pattern = WithoutUnderscores(Lower(pattern_text));
		if (pattern.empty())
			return false;
		if (pattern == "depth")
			return FormatIsDepth(format);
		if (pattern == "hdr" || pattern == "float")
			return FormatIsFloat(format);
		if (pattern == "srgb")
			return FormatIsSrgb(format);

		const char *raw = FormatName(format);
		if (raw == nullptr)
			return false;
		const std::string name = Lower(raw);
		if (name.empty() || name == "unknown")
			return false;
		if (WithoutUnderscores(name).find(pattern) != std::string::npos)
			return true;
		for (const std::string &alias : CompactAliases(name))
			if (alias == pattern || (pattern.size() >= 3 && StartsWith(alias, pattern)))
				return true;
		return false;
	}

	std::unordered_map<uint32_t, ShaderFrameFacts> CollectShaderFacts(const SessionModel &model)
	{
		std::unordered_map<uint32_t, ShaderFrameFacts> facts;
		const FrameInfo &frame = model.LastFrame();
		for (const FrameEvent &event : frame.events)
		{
			if (!IsDrawOrDispatch(event.kind) || event.pipeline_id == 0)
				continue;
			const std::vector<uint32_t> shaders = model.ShadersOfPipeline(event.pipeline_id);
			if (shaders.empty())
				continue;

			const bool draw = event.kind == EventKind::draw || event.kind == EventKind::draw_indexed ||
				event.kind == EventKind::draw_indirect || event.kind == EventKind::dispatch_mesh;
			const CommandState *state = frame.DrawStateOfEvent(event.index);

			for (const uint32_t shader_id : shaders)
			{
				const ShaderInfo *shader = model.ShaderById(shader_id);
				// Colour targets are written by the pixel shader, not by the vertex shader of the same
				// pipeline; a depth target belongs to the whole pipeline, which rasterises into it.
				const bool pixel = shader != nullptr && shader->stage == ShaderStage::pixel;
				ShaderFrameFacts &entry = facts[shader_id];
				if (draw && pixel && event.primary_resource != 0 && !Contains(entry.written, event.primary_resource))
					entry.written.push_back(event.primary_resource);
				if (draw && event.secondary_resource != 0)
				{
					// The depth target, when the resource really is one.
					const ResourceInfo *depth = model.ResourceById(event.secondary_resource);
					if (depth != nullptr && (FormatIsDepth(depth->format) || (depth->usage_flags & kUsageDepthStencil) != 0))
					{
						entry.depth_target = true;
						if (!Contains(entry.written, event.secondary_resource))
							entry.written.push_back(event.secondary_resource);
					}
				}
				if (state == nullptr)
					continue;

				entry.described = true;
				for (const DrawBinding &binding : state->bindings)
				{
					if (binding.resource_id == 0)
						continue;
					// A binding for another stage of the same pipeline is not this shader's.
					const ShaderStage stage = static_cast<ShaderStage>(binding.stage);
					if (stage != ShaderStage::unknown && shader != nullptr && stage != shader->stage)
						continue;
					const SlotKind kind = static_cast<SlotKind>(binding.kind);
					if ((kind == SlotKind::render_target && pixel) || kind == SlotKind::unordered_access)
					{
						if (!Contains(entry.written, binding.resource_id))
							entry.written.push_back(binding.resource_id);
					}
					else if (kind == SlotKind::depth_stencil)
					{
						entry.depth_target = true;
					}
					else if (kind == SlotKind::shader_resource)
					{
						if (!Contains(entry.read, binding.resource_id))
							entry.read.push_back(binding.resource_id);
					}
				}
			}
		}
		return facts;
	}

	bool MatchesShaderQuery(const ShaderQuery &query, const ShaderInfo &shader, const ShaderFrameFacts *facts,
	                        const SessionModel &model, const ShaderNotesView *notes, bool is_new)
	{
		if (!query.stages.empty() && std::find(query.stages.begin(), query.stages.end(), shader.stage) == query.stages.end())
			return false;

		const uint32_t calls = shader.draws_this_frame + shader.dispatches_this_frame;
		if (query.used && calls == 0)
			return false;
		if (query.disabled && !shader.disabled)
			return false;
		if (query.replaced && !shader.replaced && !shader.highlighted)
			return false;
		if (query.new_only && !is_new)
			return false;
		if (!query.calls.Accepts(static_cast<double>(calls)))
			return false;
		if (query.gpu_ms.active)
		{
			if (shader.gpu_ticks == 0 || !model.Timings().IsValid())
				return false;
			if (!query.gpu_ms.Accepts(model.Timings().Milliseconds(shader.gpu_ticks)))
				return false;
		}

		if (!query.format.empty() &&
		    (query.format == "dxil") != (shader.format == ShaderFormat::dxil))
			return false;
		if (!query.shader_model.empty() && !StartsWith(ShaderModelName(shader.shader_model), query.shader_model))
			return false;

		for (const std::string &pattern : query.writes)
		{
			bool found = false;
			if (facts != nullptr)
			{
				if (pattern == "depth" && facts->depth_target)
					found = true;
				for (size_t i = 0; !found && i < facts->written.size(); ++i)
					found = ResourceMatches(model, facts->written[i], pattern);
			}
			if (!found)
				return false;
		}
		for (const std::string &pattern : query.reads)
		{
			bool found = false;
			if (facts != nullptr)
				for (size_t i = 0; !found && i < facts->read.size(); ++i)
					found = ResourceMatches(model, facts->read[i], pattern);
			if (!found)
				return false;
		}

		if (query.NeedsNotes())
		{
			if (notes == nullptr)
				return false;
			for (const std::string &tag : query.tags)
			{
				bool found = false;
				for (const std::string &own : notes->tags)
					found = found || Lower(own).find(tag) != std::string::npos;
				if (!found)
					return false;
			}
			for (const std::string &name : query.names)
				if (Lower(notes->name).find(name) == std::string::npos)
					return false;
		}

		if (!query.words.empty())
		{
			const std::string searchable = Lower(std::string(ShaderStageName(shader.stage)) + " " + shader.signature.ToHex() +
				" #" + std::to_string(shader.id) + (notes != nullptr ? " " + notes->name : std::string()));
			for (const std::string &word : query.words)
				if (searchable.find(word) == std::string::npos)
					return false;
		}
		return true;
	}
}
