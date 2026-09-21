// CyGPUInjector — applying the loaded packages through the ReShade add-on API.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#include "Apply.hpp"

#include "Log.hpp"
#include "ModLibrary.hpp"

#include <reshade.hpp>

#include <CyGPUInspectorCore/ShaderBlob.hpp>

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

using namespace reshade::api;

namespace cygi
{
	namespace
	{
		ShaderStage StageOfSubobject(pipeline_subobject_type type)
		{
			switch (type)
			{
			case pipeline_subobject_type::vertex_shader: return ShaderStage::vertex;
			case pipeline_subobject_type::hull_shader: return ShaderStage::hull;
			case pipeline_subobject_type::domain_shader: return ShaderStage::domain;
			case pipeline_subobject_type::geometry_shader: return ShaderStage::geometry;
			case pipeline_subobject_type::pixel_shader: return ShaderStage::pixel;
			case pipeline_subobject_type::compute_shader: return ShaderStage::compute;
			case pipeline_subobject_type::amplification_shader: return ShaderStage::amplification;
			case pipeline_subobject_type::mesh_shader: return ShaderStage::mesh;
			case pipeline_subobject_type::raygen_shader: return ShaderStage::raygen;
			case pipeline_subobject_type::any_hit_shader: return ShaderStage::any_hit;
			case pipeline_subobject_type::closest_hit_shader: return ShaderStage::closest_hit;
			case pipeline_subobject_type::miss_shader: return ShaderStage::miss;
			case pipeline_subobject_type::intersection_shader: return ShaderStage::intersection;
			case pipeline_subobject_type::callable_shader: return ShaderStage::callable;
			default: return ShaderStage::unknown;
			}
		}

		bool IsShaderSubobject(pipeline_subobject_type type)
		{
			return StageOfSubobject(type) != ShaderStage::unknown;
		}

		// Pipelines built from a shader a package asked to disable. Written while the game creates
		// pipelines, read on every draw, so the two are separated by a shared mutex rather than by
		// hope. It stays empty, and is never consulted, when no package disables anything.
		std::shared_mutex g_disabled_mutex;
		std::unordered_set<uint64_t> g_disabled_pipelines;

		// What each command list currently has bound, so a draw knows which pipeline it is about
		// to use. ReShade gives one private data slot per command list; that is all this needs.
		struct __declspec(uuid("2b0f9a41-6c3e-4f7a-9d21-58c4f0a1e7b3")) BoundPipelines
		{
			uint64_t graphics = 0;
			uint64_t compute = 0;
		};

		BoundPipelines *BoundOf(command_list *command_list)
		{
			return command_list != nullptr ? command_list->get_private_data<BoundPipelines>() : nullptr;
		}

		bool IsDisabled(uint64_t pipeline)
		{
			if (pipeline == 0)
				return false;
			std::shared_lock<std::shared_mutex> lock(g_disabled_mutex);
			return g_disabled_pipelines.count(pipeline) != 0;
		}

		// ------------------------------------------------------------------------------------
		// Replacement, at creation
		// ------------------------------------------------------------------------------------

		bool OnCreatePipeline(device * /*device*/, pipeline_layout /*layout*/, uint32_t subobject_count,
		                      const pipeline_subobject *subobjects)
		{
			ModLibrary &library = Library();
			if (library.Empty())
				return false;

			bool modified = false;
			for (uint32_t i = 0; i < subobject_count; ++i)
			{
				if (!IsShaderSubobject(subobjects[i].type) || subobjects[i].data == nullptr)
					continue;

				// `subobjects` is const, but what `data` points at is not: that is how ReShade
				// lets an add-on change a pipeline description before the graphics API sees it.
				auto *desc = static_cast<shader_desc *>(subobjects[i].data);
				if (desc->code == nullptr || desc->code_size == 0)
					continue;

				const Sha256Digest hash = ComputeShaderSemanticHash(desc->code, desc->code_size);
				const ModRule *rule = library.Find(hash);
				if (rule == nullptr || rule->action != ModAction::replace)
					continue;
				if (rule->byte_code == nullptr || rule->byte_code_size == 0)
					continue;

				// The package owns this memory for the life of the process, and ReShade only
				// needs it to stay valid until the pipeline is created, so no copy is made.
				desc->code = rule->byte_code;
				desc->code_size = rule->byte_code_size;
				modified = true;

				library.CountReplacement(rule->package_index, rule->entry_index);
				log::Debug("Replaced shader %s", hash.ToShortHex(8).c_str());
			}

			// `true` tells ReShade the description was changed and that it should create the
			// pipeline from the modified one.
			return modified;
		}

		// ------------------------------------------------------------------------------------
		// Suppression, at draw time
		// ------------------------------------------------------------------------------------

		void OnInitPipeline(device * /*device*/, pipeline_layout /*layout*/, uint32_t subobject_count,
		                    const pipeline_subobject *subobjects, pipeline pipeline)
		{
			ModLibrary &library = Library();
			if (!library.AnyDisableRule() || pipeline.handle == 0)
				return;

			for (uint32_t i = 0; i < subobject_count; ++i)
			{
				if (!IsShaderSubobject(subobjects[i].type) || subobjects[i].data == nullptr)
					continue;

				const auto *desc = static_cast<const shader_desc *>(subobjects[i].data);
				if (desc->code == nullptr || desc->code_size == 0)
					continue;

				const ModRule *rule = library.Find(ComputeShaderSemanticHash(desc->code, desc->code_size));
				if (rule == nullptr || rule->action != ModAction::disable)
					continue;

				std::unique_lock<std::shared_mutex> lock(g_disabled_mutex);
				g_disabled_pipelines.insert(pipeline.handle);
				break;   // one disabled shader is enough to drop every draw of this pipeline
			}
		}

		void OnDestroyPipeline(device * /*device*/, pipeline pipeline)
		{
			if (pipeline.handle == 0)
				return;
			std::unique_lock<std::shared_mutex> lock(g_disabled_mutex);
			g_disabled_pipelines.erase(pipeline.handle);
		}

		void OnInitCommandList(command_list *command_list)
		{
			command_list->create_private_data<BoundPipelines>();
		}

		void OnDestroyCommandList(command_list *command_list)
		{
			command_list->destroy_private_data<BoundPipelines>();
		}

		void OnBindPipeline(command_list *command_list, pipeline_stage stages, pipeline pipeline)
		{
			BoundPipelines *bound = BoundOf(command_list);
			if (bound == nullptr)
				return;

			if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(pipeline_stage::compute_shader)) != 0)
				bound->compute = pipeline.handle;
			if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(pipeline_stage::all_graphics)) != 0)
				bound->graphics = pipeline.handle;
		}

		// Returning true makes ReShade drop the command.
		bool SkipIfDisabled(command_list *command_list, bool compute)
		{
			const BoundPipelines *bound = BoundOf(command_list);
			if (bound == nullptr)
				return false;

			if (!IsDisabled(compute ? bound->compute : bound->graphics))
				return false;

			Library().CountSkippedDraw();
			return true;
		}

		bool OnDraw(command_list *command_list, uint32_t, uint32_t, uint32_t, uint32_t)
		{
			return SkipIfDisabled(command_list, false);
		}

		bool OnDrawIndexed(command_list *command_list, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
		{
			return SkipIfDisabled(command_list, false);
		}

		bool OnDispatch(command_list *command_list, uint32_t, uint32_t, uint32_t)
		{
			return SkipIfDisabled(command_list, true);
		}

		bool OnDispatchMesh(command_list *command_list, uint32_t, uint32_t, uint32_t)
		{
			return SkipIfDisabled(command_list, false);
		}

		bool OnDrawOrDispatchIndirect(command_list *command_list, indirect_command type, resource,
		                              uint64_t, uint32_t, uint32_t)
		{
			return SkipIfDisabled(command_list, type == indirect_command::dispatch);
		}
	}

	void RegisterModEvents()
	{
		// Replacement is registered whatever the packages contain, because it is one hash lookup
		// per shader at pipeline creation and pipelines are created a few thousand times in a
		// whole run, not per frame.
		reshade::register_event<reshade::addon_event::create_pipeline>(OnCreatePipeline);

		// The suppression path costs something per draw, so it is only wired up when a package
		// actually asks for it. A package that only replaces shaders leaves the draw callbacks
		// unregistered, and ReShade then never calls into this add-on during a frame at all.
		if (!Library().AnyDisableRule())
		{
			log::Info("No package disables a shader: the per draw path stays unregistered");
			return;
		}

		reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
		reshade::register_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);
		reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
		reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
		reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
		reshade::register_event<reshade::addon_event::draw>(OnDraw);
		reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
		reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
		reshade::register_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh);
		reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);
	}

	void UnregisterModEvents()
	{
		reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);
		reshade::unregister_event<reshade::addon_event::dispatch_mesh>(OnDispatchMesh);
		reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
		reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
		reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
		reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);
		reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
		reshade::unregister_event<reshade::addon_event::init_command_list>(OnInitCommandList);
		reshade::unregister_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);
		reshade::unregister_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
		reshade::unregister_event<reshade::addon_event::create_pipeline>(OnCreatePipeline);

		std::unique_lock<std::shared_mutex> lock(g_disabled_mutex);
		g_disabled_pipelines.clear();
	}
}
