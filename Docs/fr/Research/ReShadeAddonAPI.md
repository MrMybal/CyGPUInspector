# Recherche — API Add-on ReShade

*Disponible aussi en [anglais](../../Research/ReShadeAddonAPI.md), qui fait foi.*

Source de vérité : en-têtes du SDK ReShade vendorisés dans `ThirdParty/reshade/include/`
(`RESHADE_API_VERSION 20`, ReShade 6.8.x, licence BSD-3-Clause). Tout ce qui suit a été lu dans
ces en-têtes, pas supposé.

## 1. Ce que l'API garantit

| Besoin CyGPUInspector | Mécanisme ReShade | Verdict |
|---|---|---|
| Charger du code dans le jeu | `.addon64` chargé par ReShade, exports `AddonInit` / `AddonUninit`, `reshade::register_addon` | **Disponible** — aucun hook maison |
| Détecter l'API graphique | `device::get_api()` → `d3d9/d3d10/d3d11/d3d12/opengl/vulkan` | Disponible |
| Récupérer le bytecode compilé des shaders | `create_pipeline` / `init_pipeline` → `pipeline_subobject[]` avec `shader_desc{ code, code_size, entry_point }` | **Disponible** (voir §3) |
| Associer un handle de pipeline aux shaders | `init_pipeline(device, layout, subobject_count, subobjects, pipeline)` | Disponible |
| Suivre draws / dispatches | `draw`, `draw_indexed`, `dispatch`, `dispatch_mesh`, `dispatch_rays`, `draw_or_dispatch_indirect` | Disponible |
| Annuler un draw / dispatch | Le callback retourne `bool` : `true` = commande supprimée | **Disponible** |
| Remplacer un shader | `create_pipeline` : modifier les `subobjects` et retourner `true` | **Disponible** (voir §4) |
| Suivre les render targets | `bind_render_targets_and_depth_stencil`, `begin_render_pass` / `end_render_pass` | Disponible |
| Suivre les bindings de ressources | `push_descriptors`, `bind_descriptor_tables`, `update_descriptor_tables`, `copy_descriptor_tables`, `push_constants` | Disponible, mais coûteux (voir §5) |
| Suivre les buffers d'entrée | `bind_index_buffer`, `bind_vertex_buffers`, `bind_stream_output_buffers` | Disponible |
| Suivre copies / résolutions / clears | `copy_resource`, `copy_buffer_region`, `copy_texture_region`, `copy_buffer_to_texture`, `copy_texture_to_buffer`, `resolve_texture_region`, `clear_*`, `generate_mipmaps` | Disponible |
| Suivre les barrières | `barrier(cmd, count, resources, old_states, new_states)` | Disponible (D3D12 / Vulkan) |
| Frontières de frame | `present`, `finish_present`, `reshade_present` | Disponible |
| Timings GPU | `device::create_query_heap(query_type::timestamp, …)`, `command_list::begin_query` / `end_query`, `device::get_query_heap_results`, `command_queue::get_timestamp_frequency()` | **Disponible** |
| Textures partagées inter-processus | `device::create_resource(desc, init, state, out, void **shared_handle)` avec `resource_flags::shared` / `shared_nt_handle` | **Disponible** |
| Fences partagées | `device::create_fence(value, fence_flags, out, void **shared_handle)` | Disponible |
| Handles natifs D3D | `device::get_native()`, et `resource::handle` / `resource_view::handle` contiennent le pointeur natif | Disponible |
| Overlay | `reshade::register_overlay(title, callback)` + table de fonctions ImGui | Disponible |

## 2. Liste complète des événements (API 20)

`init/create/destroy` pour : device, command_list, command_queue, swapchain, effect_runtime,
sampler, resource, resource_view, pipeline, pipeline_layout, query_heap.

Commandes : `barrier`, `begin_render_pass`, `end_render_pass`,
`bind_render_targets_and_depth_stencil`, `bind_pipeline`, `bind_pipeline_states`,
`bind_viewports`, `bind_scissor_rects`, `push_constants`, `push_descriptors`,
`bind_descriptor_tables`, `bind_index_buffer`, `bind_vertex_buffers`,
`bind_stream_output_buffers`, `draw`, `draw_indexed`, `dispatch`, `dispatch_mesh`,
`dispatch_rays`, `draw_or_dispatch_indirect`, `copy_*`, `resolve_texture_region`, `clear_*`,
`generate_mipmaps`, `begin_query`, `end_query`, `copy_query_heap_results`,
`*_acceleration_structure`, `reset_command_list`, `close_command_list`, `execute_command_list`,
`execute_secondary_command_list`, `map_*`, `unmap_*`, `update_buffer_region`,
`update_texture_region`, `present`, `finish_present`, `set_fullscreen_state`, plus les
événements `reshade_*` (effets, overlay, screenshot).

Tous les événements de la liste demandée dans le cahier des charges (§5) existent, **sauf** une
notion explicite de « Sampler Bind » séparée : en D3D12 / Vulkan les samplers passent par
`push_descriptors` / `bind_descriptor_tables` avec `descriptor_type::sampler`, et en D3D11 ils
arrivent aussi par `push_descriptors`. Il n'y a pas non plus de « Begin Frame » : la frontière de
frame est `present` (fin de frame N, début de frame N+1).

## 3. Accès au bytecode des shaders — points critiques

* `shader_desc::code` est un pointeur **valide uniquement pendant le callback**. Il faut copier
  les `code_size` octets immédiatement.
* En D3D11, un « pipeline » ReShade correspond à **un seul** objet d'état (un
  `ID3D11PixelShader`, ou un `ID3D11BlendState`…). Un pipeline y a donc typiquement 1 sous-objet
  shader.
* En D3D12, `CreateGraphicsPipelineState` produit **un** pipeline avec plusieurs sous-objets
  shader (VS+PS+…). Le mapping shader → pipeline est donc 1..N.
* En OpenGL, `code` est du **texte GLSL** (`glShaderSource`), pas du binaire.
* En Vulkan, `code` est du SPIR-V.
* `init_pipeline` donne le handle de pipeline final + les sous-objets : c'est l'événement qui
  permet de construire la table `pipeline handle → [signatures de shaders]` utilisée ensuite par
  `bind_pipeline`.
* `destroy_pipeline` n'est pas appelé en D3D9.

## 4. Remplacement de shader

Le remplacement se fait dans `create_pipeline` en modifiant `subobjects[i].data->code` /
`code_size` avant que ReShade ne transmette la création à l'API. Conséquences :

* Un shader ne peut être remplacé **qu'au moment de sa création**. Pour remplacer un shader déjà
  créé, il faut soit attendre une recréation, soit créer un pipeline de remplacement via
  `device::create_pipeline` et l'échanger dans `bind_pipeline` (retour `true` + rebind).
* C'est la seconde stratégie qui permet un remplacement « à chaud » sans redémarrer le jeu, et
  c'est elle que CyGPUInspectorRS utilise (voir `Docs/ShaderReplacement.md`).

## 5. Coût des événements

ReShade n'appelle les callbacks que pour les événements réellement enregistrés : ne pas
enregistrer `push_descriptors` coûte zéro. Classement par coût :

1. `push_descriptors` / `bind_descriptor_tables` / `update_descriptor_tables` — les plus
   fréquents, plusieurs milliers d'appels par frame. **Activés seulement pendant une capture.**
2. `draw` / `draw_indexed` / `dispatch` — quelques milliers par frame. Activés en tracking normal
   mais avec un traitement O(1) sans allocation.
3. `bind_pipeline`, `bind_render_targets_and_depth_stencil` — quelques centaines à milliers.
4. `init_*` / `destroy_*` / `create_pipeline` — rares (création de ressources).

## 6. Pièges vérifiés (SDK + projet CyGameCapture du même auteur)

* En D3D11, le contexte immédiat est **à la fois** `command_queue` et `command_list` : ReShade
  renvoie le même objet, les `private_data` sont donc partagées.
* `reshade_begin_effects` n'est déclenché que si des effets sont chargés → inutilisable comme
  frontière de frame fiable.
* `reshade_overlay` est appelé chaque frame dès qu'un overlay est enregistré, même fermé :
  l'état d'ouverture se suit avec `reshade_open_overlay`.
* `ImTextureID` doit être défini à `ImU64` : dans l'overlay ReShade, une texture ImGui est un
  handle de `resource_view`.
* `destroy_pipeline` / `destroy_resource` peuvent être appelés depuis l'intérieur d'autres appels
  API : prudence avec les verrous.
* `create_resource` avec `resource_flags::shared` renvoie en D3D11 le handle DXGI *legacy*
  (`IDXGIResource::GetSharedHandle`) ; en D3D12 seul `shared_nt_handle` est supporté.

## 7. Ce que ReShade ne donne pas

* **Pas de nom de passe ni de marqueur de debug** : `PIXBeginEvent`,
  `ID3DUserDefinedAnnotation` et `vkCmdBeginDebugUtilsLabelEXT` ne sont pas exposés par l'API
  add-on. Le frame graph doit donc être **déduit** des dépendances de ressources, pas lu dans le
  jeu.
* Pas d'accès direct au contenu d'un constant buffer : il faut intercepter `push_constants` /
  `update_buffer_region` / `map_buffer_region`, ou copier le buffer vers un staging buffer.
* Pas de désassembleur ni de réflexion de shader : c'est à nous (DXC / D3DReflect).
* Pas d'accès au source HLSL original du jeu — il n'existe pas dans le binaire.
