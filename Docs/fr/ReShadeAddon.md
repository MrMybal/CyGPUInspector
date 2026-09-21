# CyGPUInspectorRS — l'agent dans le jeu

*Disponible aussi en [anglais](../ReShadeAddon.md), qui fait foi.*

**État : milestone 1 implémenté.**

## Rôle

`Hook · Track · Capture · Control · Transfer` — rien d'autre. Aucune base de données, aucun
décompilateur, aucune analyse, aucune interface d'analyse. Tout cela est dans
[CyGPUInspectorApp](StandaloneApp.md).

L'add-on **n'implémente aucun mécanisme d'injection**. Il utilise l'API add-on officielle de
ReShade (`reshade::register_addon`, `reshade::register_event`), et rien d'autre. Là où ReShade ne
peut pas se charger, CyGPUInspector n'est simplement pas supporté.

## Structure

```
CyGPUInspectorRS/Source
├── Addon/      AddonMain (exports .addon64), Log, Config ([CYGPUINSPECTOR] dans ReShade.ini),
│               DeviceContext (état par device + travail par frame), .rc
├── Tracking/   ShaderTracker, ResourceTracker, CommandListState, FrameRecorder
├── Control/    RuntimeControl (disable / highlight, table plate sans verrou)
├── Ipc/        IpcServer (ring + pipe + annuaire de sessions)
└── UI/         Overlay (mini panneau ReShade)
```

## État par objet

| Objet | Donnée privée | Contenu |
|---|---|---|
| `device` | `DeviceContext` | trackers, IPC, contrôle runtime, niveau de tracking |
| `command_list` | `CommandListState` | bindings courants + tampon local d'événements, **sans verrou** |

Une command list n'est enregistrée que par un thread à la fois : son tampon n'a donc pas besoin de
protection. Il est fusionné dans le `FrameRecorder` :

* sur `execute_command_list` / `execute_secondary_command_list` (D3D12, contextes différés) ;
* sur `present` pour le contexte immédiat D3D11, qui n'exécute jamais de command list. ReShade
  expose ce contexte à la fois comme `command_queue` et comme `command_list` : `init_command_queue`
  récupère `get_immediate_command_list()` et l'enregistre dans la liste à fusionner.

## Événements enregistrés

`init/destroy_device`, `init/destroy_command_list`, `init/destroy_command_queue`, `init_swapchain`,
`init/destroy_pipeline`, `init/destroy_resource`, `init/destroy_resource_view`, `bind_pipeline`,
`bind_render_targets_and_depth_stencil`, `begin/end_render_pass`, `draw`, `draw_indexed`,
`dispatch`, `dispatch_mesh`, `draw_or_dispatch_indirect`, `copy_resource`, `copy_texture_region`,
`resolve_texture_region`, `clear_render_target_view`, `clear_depth_stencil_view`,
`clear_unordered_access_view_float/uint`, `generate_mipmaps`, `reset_command_list`,
`execute_command_list`, `execute_secondary_command_list`, `present`.

Non enregistrés pour l'instant, parce qu'ils sont les plus coûteux et ne servent qu'en mode
capture : `push_descriptors`, `bind_descriptor_tables`, `update_descriptor_tables`,
`push_constants`, `bind_viewports`, `bind_scissor_rects`, `bind_vertex_buffers`,
`bind_index_buffer`, `barrier`, `map_*`.

## Travail par frame (`present`)

1. Fusion des tampons du contexte immédiat.
2. Traitement des commandes reçues du standalone (sur ce thread, jamais sur le thread du pipe).
3. Publication des nouveaux shaders (bytecode inclus, une seule fois), pipelines et ressources.
4. Publication de la frame : `frame_begin`, `frame_events` par blocs, `frame_end`.
5. Toutes les 60 frames : `stats` (coût de l'outil lui-même).
6. Signal au lecteur, heartbeat dans l'annuaire, incrément de l'index de frame.

Le temps passé dans cette fonction est mesuré et renvoyé dans `FrameEndRecord::addon_cpu_ms` :
le coût de l'outil est visible dans l'UI, pas caché.

## Niveau de tracking

`Idle`, `Tracking` (défaut), `PassTiming`, `Capture`, `FullDrawTiming` — changeable à chaud depuis
l'overlay ou le standalone. Aujourd'hui le niveau contrôle la publication ; le
désenregistrement effectif des callbacks coûteux arrive avec les milestones 4 et 8.

## Overlay

Volontairement minimal (§13–§14 du cahier des charges) : état de la connexion, niveau de tracking,
compteurs, coût IPC, deux boutons (`Resend everything`, `Restore all shaders`). Le vrai travail se
fait dans le standalone, dans son propre processus.

## Configuration

Section `[CYGPUINSPECTOR]` de `ReShade.ini` :

| Clé | Défaut | Effet |
|---|---|---|
| `Verbose` | `0` | journalisation détaillée dans le log ReShade |
| `MaxEventsPerFrame` | `250000` | garde-fou mémoire pour une frame pathologique |

## Installation

Copier `CyGPUInspectorRS.addon64` à côté de l'exécutable du jeu (ou dans le dossier `AddonPath`
de ReShade). ReShade doit être une build **add-on enabled**.
