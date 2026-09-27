// CyGPUInspectorDecompiler — can this byte code take the place of that one in the game's pipeline?
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "CyGPUInspectorDecompiler/ReplacementCheck.hpp"

#include "CyGPUInspectorDecompiler/Disassembler.hpp"

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include <algorithm>
#include <cctype>

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

		std::string MaskText(uint8_t mask)
		{
			std::string text;
			for (int i = 0; i < 4; ++i)
				if (mask & (1u << i))
					text += "xyzw"[i];
			return text.empty() ? std::string("-") : text;
		}

		// Semantic names are case-insensitive: TEXCOORD and texcoord are the same input.
		const SignatureElement *FindSemantic(const std::vector<SignatureElement> &list, const SignatureElement &wanted)
		{
			const std::string name = Lower(wanted.semantic_name);
			for (const SignatureElement &element : list)
				if (element.semantic_index == wanted.semantic_index && Lower(element.semantic_name) == name)
					return &element;
			return nullptr;
		}

		std::string Element(const SignatureElement &element, char register_letter)
		{
			return element.semantic_name + std::to_string(element.semantic_index) + " (" + register_letter +
				std::to_string(element.register_index) + "." + MaskText(element.mask) + ", " + element.component_type + ")";
		}

		// The register class a binding lives in, which is what a root signature is organised by.
		char RegisterClass(BindingKind kind)
		{
			switch (kind)
			{
			case BindingKind::constant_buffer: return 'b';
			case BindingKind::sampler: return 's';
			case BindingKind::unordered_access:
			case BindingKind::append_buffer:
			case BindingKind::consume_buffer: return 'u';
			default: return 't';
			}
		}

		std::string Binding(const ShaderBinding &binding)
		{
			std::string text = (binding.name.empty() ? std::string("?") : binding.name) + " (" +
				RegisterClass(binding.kind) + std::to_string(binding.bind_point);
			if (binding.bind_count > 1)
				text += "-" + std::to_string(binding.bind_point + binding.bind_count - 1);
			if (binding.space != 0)
				text += ", space " + std::to_string(binding.space);
			return text + ")";
		}

		// A count of 0 is an unbounded array: it covers everything from its first register.
		bool Covers(const ShaderBinding &original, const ShaderBinding &wanted)
		{
			if (RegisterClass(original.kind) != RegisterClass(wanted.kind) || original.space != wanted.space)
				return false;
			const uint64_t first = original.bind_point;
			const uint64_t last = original.bind_count == 0 ? UINT32_MAX : first + original.bind_count - 1;
			const uint64_t wanted_first = wanted.bind_point;
			const uint64_t wanted_last = wanted.bind_count == 0 ? UINT32_MAX : wanted_first + wanted.bind_count - 1;
			return wanted_first >= first && wanted_last <= last;
		}
	}

	std::string ReplacementCheck::Describe() const
	{
		std::string text;
		for (int pass = 0; pass < 2; ++pass)
			for (const ReplacementIssue &issue : issues)
				if (issue.blocking == (pass == 0))
					text += (text.empty() ? "" : "\n") + std::string(issue.blocking ? "- " : "- (warning) ") + issue.message;
		return text;
	}

	ReplacementCheck CheckReplacement(const void *original, size_t original_size,
	                                  const void *replacement, size_t replacement_size)
	{
		ReplacementCheck check;
		auto block = [&check](std::string message) {
			check.issues.push_back({ true, std::move(message) });
			check.compatible = false;
		};
		auto warn = [&check](std::string message) { check.issues.push_back({ false, std::move(message) }); };

		ShaderBlobInfo before;
		ShaderBlobInfo after;
		if (!ParseShaderBlob(original, original_size, before) || !ParseShaderBlob(replacement, replacement_size, after) ||
		    !before.valid || !after.valid)
		{
			warn("the byte code could not be read, so the replacement was not checked");
			return check;
		}
		if (before.format != after.format)
			block(std::string("the original is ") + ShaderFormatName(before.format) + " and the replacement " +
				ShaderFormatName(after.format) + ": compile it for the original's shader model");
		if (before.stage != after.stage)
			block(std::string("the original is a ") + ShaderStageName(before.stage) + " shader and the replacement a " +
				ShaderStageName(after.stage) + " shader");
		if (!check.compatible)
			return check;

		const ReflectionResult old_shader = Reflect(original, original_size);
		const ReflectionResult new_shader = Reflect(replacement, replacement_size);
		if (!old_shader.ok || !new_shader.ok)
		{
			warn("reflection is not available (" + (old_shader.ok ? new_shader.error : old_shader.error) +
				"), so inputs, outputs and resources were not checked");
			return check;
		}

		// Inputs come from the previous stage, or the input assembler: only what it provides exists.
		for (const SignatureElement &input : new_shader.inputs)
		{
			const SignatureElement *old = FindSemantic(old_shader.inputs, input);
			if (old == nullptr)
			{
				block("input " + Element(input, 'v') + " does not exist in the original: nothing before this stage "
					"provides it");
				continue;
			}
			if (old->register_index != input.register_index)
				block("input " + Element(input, 'v') + " is in another register than in the original, " + Element(*old, 'v'));
			else if ((input.mask & ~old->mask) != 0)
				block("input " + Element(input, 'v') + " has components the original does not have, " + Element(*old, 'v') +
					": declare it with the original's size");
			else if (old->component_type != input.component_type)
				block("input " + Element(input, 'v') + " has another type than in the original, " + Element(*old, 'v'));
		}

		// Outputs feed the next stage, except for a pixel shader, whose outputs go to the targets.
		const bool feeds_next_stage = after.stage != ShaderStage::pixel && after.stage != ShaderStage::compute;
		for (const SignatureElement &output : old_shader.outputs)
		{
			const SignatureElement *now = FindSemantic(new_shader.outputs, output);
			const std::string what = "output " + Element(output, 'o');
			if (now == nullptr)
			{
				if (feeds_next_stage)
					block(what + " of the original is not written: the next stage reads it");
				else
					warn(what + " of the original is not written");
				continue;
			}
			if (now->register_index != output.register_index)
			{
				if (feeds_next_stage)
					block(what + " moved to another register, " + Element(*now, 'o'));
				else
					warn(what + " moved to another register, " + Element(*now, 'o'));
			}
			else if (feeds_next_stage && (output.mask & ~now->mask) != 0)
			{
				block(what + " is written with fewer components, " + Element(*now, 'o'));
			}
		}
		for (const SignatureElement &output : new_shader.outputs)
			if (FindSemantic(old_shader.outputs, output) == nullptr)
				warn("output " + Element(output, 'o') + " is new: the original does not write it");

		// Resources: the pipeline's root signature, or the game's bindings, only have what the
		// original declares. A new register has nothing behind it.
		for (const ShaderBinding &binding : new_shader.bindings)
		{
			const ShaderBinding *home = nullptr;
			for (const ShaderBinding &old : old_shader.bindings)
				if (Covers(old, binding))
				{
					home = &old;
					break;
				}
			if (home == nullptr)
			{
				block("resource " + Binding(binding) + " is not declared by the original: the game's pipeline has no "
					"place for it");
				continue;
			}
			if (binding.kind == BindingKind::constant_buffer && home->size != 0 && binding.size > home->size)
				warn("constant buffer " + Binding(binding) + " is " + std::to_string(binding.size) + " bytes, the "
					"original's " + std::to_string(home->size) + ": what lies beyond reads as zero");
			else if (binding.kind != home->kind && RegisterClass(binding.kind) == 't')
				block("resource " + Binding(binding) + " is a " + BindingKindName(binding.kind) + ", the original's " +
					Binding(*home) + " a " + BindingKindName(home->kind));
		}
		return check;
	}
}
