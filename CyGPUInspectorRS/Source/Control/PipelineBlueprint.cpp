// CyGPUInspectorRS — pipeline blueprints.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "PipelineBlueprint.hpp"

#include <cstring>

using namespace reshade::api;

namespace cygi
{
	namespace
	{
		// Size of one element of a sub-object array, or 0 when we do not know how to copy it.
		size_t ElementSize(pipeline_subobject_type type)
		{
			switch (type)
			{
			case pipeline_subobject_type::vertex_shader:
			case pipeline_subobject_type::hull_shader:
			case pipeline_subobject_type::domain_shader:
			case pipeline_subobject_type::geometry_shader:
			case pipeline_subobject_type::pixel_shader:
			case pipeline_subobject_type::compute_shader:
			case pipeline_subobject_type::amplification_shader:
			case pipeline_subobject_type::mesh_shader:
			case pipeline_subobject_type::raygen_shader:
			case pipeline_subobject_type::any_hit_shader:
			case pipeline_subobject_type::closest_hit_shader:
			case pipeline_subobject_type::miss_shader:
			case pipeline_subobject_type::intersection_shader:
			case pipeline_subobject_type::callable_shader:
				return sizeof(shader_desc);
			case pipeline_subobject_type::input_layout:
				return sizeof(input_element);
			case pipeline_subobject_type::stream_output_state:
				return sizeof(stream_output_desc);
			case pipeline_subobject_type::blend_state:
				return sizeof(blend_desc);
			case pipeline_subobject_type::rasterizer_state:
				return sizeof(rasterizer_desc);
			case pipeline_subobject_type::depth_stencil_state:
				return sizeof(depth_stencil_desc);
			case pipeline_subobject_type::primitive_topology:
				return sizeof(primitive_topology);
			case pipeline_subobject_type::depth_stencil_format:
			case pipeline_subobject_type::render_target_formats:
				return sizeof(format);
			case pipeline_subobject_type::sample_mask:
			case pipeline_subobject_type::sample_count:
			case pipeline_subobject_type::viewport_count:
			case pipeline_subobject_type::max_vertex_count:
			case pipeline_subobject_type::max_payload_size:
			case pipeline_subobject_type::max_attribute_size:
			case pipeline_subobject_type::max_recursion_depth:
				return sizeof(uint32_t);
			case pipeline_subobject_type::dynamic_pipeline_states:
				return sizeof(dynamic_state);
			case pipeline_subobject_type::flags:
				return sizeof(pipeline_flags);
			default:
				return 0;
			}
		}

		bool IsShaderSubobject(pipeline_subobject_type type)
		{
			return ElementSize(type) == sizeof(shader_desc) &&
			       type != pipeline_subobject_type::stream_output_state &&
			       (type == pipeline_subobject_type::vertex_shader ||
			        type == pipeline_subobject_type::hull_shader ||
			        type == pipeline_subobject_type::domain_shader ||
			        type == pipeline_subobject_type::geometry_shader ||
			        type == pipeline_subobject_type::pixel_shader ||
			        type == pipeline_subobject_type::compute_shader ||
			        type == pipeline_subobject_type::amplification_shader ||
			        type == pipeline_subobject_type::mesh_shader ||
			        type == pipeline_subobject_type::raygen_shader ||
			        type == pipeline_subobject_type::any_hit_shader ||
			        type == pipeline_subobject_type::closest_hit_shader ||
			        type == pipeline_subobject_type::miss_shader ||
			        type == pipeline_subobject_type::intersection_shader ||
			        type == pipeline_subobject_type::callable_shader);
		}
	}

	bool PipelineBlueprint::Capture(uint32_t subobject_count, const pipeline_subobject *source,
	                                pipeline_layout pipeline_layout, const std::vector<uint32_t> &shader_ids)
	{
		replaceable = false;
		reason.clear();
		layout = pipeline_layout;
		subobjects.clear();
		shaders.clear();
		blobs.clear();
		strings.clear();

		if (source == nullptr || subobject_count == 0)
		{
			reason = "the pipeline has no sub-objects";
			return false;
		}

		// Reserve first: the sub-object data pointers point into these vectors, so they must not
		// be reallocated while the capture is running.
		blobs.reserve(subobject_count);
		strings.reserve(subobject_count);
		subobjects.reserve(subobject_count);

		size_t shader_index = 0;
		for (uint32_t i = 0; i < subobject_count; ++i)
		{
			const pipeline_subobject &object = source[i];
			const size_t element = ElementSize(object.type);
			if (element == 0 || object.data == nullptr)
			{
				reason = "this pipeline uses a sub-object CyGPUInspector cannot copy yet";
				subobjects.clear();
				return false;
			}

			blobs.emplace_back(element * object.count);
			std::memcpy(blobs.back().data(), object.data, element * object.count);

			pipeline_subobject copy = object;
			copy.data = blobs.back().data();
			subobjects.push_back(copy);

			if (IsShaderSubobject(object.type))
			{
				shader_desc *desc = reinterpret_cast<shader_desc *>(blobs.back().data());

				// The byte code is not copied: it is fetched from the tracker when a variant is
				// built. Keeping the dangling pointer would be a trap, so it is cleared.
				desc->code = nullptr;
				desc->code_size = 0;

				ShaderSlot slot;
				slot.subobject_index = i;
				slot.shader_id = shader_index < shader_ids.size() ? shader_ids[shader_index] : 0;
				if (desc->entry_point != nullptr)
					slot.entry_point = desc->entry_point;
				desc->entry_point = nullptr;
				shaders.push_back(std::move(slot));
				++shader_index;
			}
			else if (object.type == pipeline_subobject_type::input_layout)
			{
				// Input elements point at semantic name strings that belong to the caller.
				input_element *elements = reinterpret_cast<input_element *>(blobs.back().data());
				for (uint32_t element_index = 0; element_index < object.count; ++element_index)
				{
					if (elements[element_index].semantic == nullptr)
						continue;
					strings.emplace_back(elements[element_index].semantic);
					elements[element_index].semantic = strings.back().c_str();
				}
			}
		}

		replaceable = true;
		return true;
	}

	pipeline PipelineBlueprint::CreateVariant(device *device, const CodeProvider &provide_code,
	                                          uint32_t replaced_shader_id, const void *code, size_t code_size,
	                                          std::string &error) const
	{
		if (!replaceable || device == nullptr)
		{
			error = reason.empty() ? "this pipeline cannot be rebuilt" : reason;
			return pipeline{ 0 };
		}

		// Working copies: the sub-object array is rewritten with real byte code pointers.
		std::vector<pipeline_subobject> objects = subobjects;
		std::vector<std::vector<uint8_t>> shader_code(shaders.size());
		std::vector<shader_desc> descs(shaders.size());
		std::vector<std::string> entry_points(shaders.size());

		bool replaced_any = false;
		for (size_t i = 0; i < shaders.size(); ++i)
		{
			const ShaderSlot &slot = shaders[i];
			if (slot.subobject_index >= objects.size())
			{
				error = "the captured pipeline description is inconsistent";
				return pipeline{ 0 };
			}

			if (slot.shader_id == replaced_shader_id && code != nullptr && code_size != 0)
			{
				shader_code[i].assign(static_cast<const uint8_t *>(code),
					static_cast<const uint8_t *>(code) + code_size);
				replaced_any = true;
			}
			else if (!provide_code || !provide_code(slot.shader_id, shader_code[i]))
			{
				error = "the original byte code of one stage is no longer available";
				return pipeline{ 0 };
			}

			entry_points[i] = slot.entry_point;

			descs[i] = {};
			descs[i].code = shader_code[i].data();
			descs[i].code_size = shader_code[i].size();
			descs[i].entry_point = entry_points[i].empty() ? nullptr : entry_points[i].c_str();

			objects[slot.subobject_index].data = &descs[i];
			objects[slot.subobject_index].count = 1;
		}

		if (!replaced_any)
		{
			error = "this pipeline does not use that shader";
			return pipeline{ 0 };
		}

		pipeline variant = {};
		if (!device->create_pipeline(layout, static_cast<uint32_t>(objects.size()), objects.data(), &variant))
		{
			error = "the graphics API refused the replacement pipeline";
			return pipeline{ 0 };
		}
		return variant;
	}
}
