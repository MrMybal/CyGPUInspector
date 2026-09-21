# CyGPUInspectorMCP

*Disponible aussi en [anglais](../MCP.md), qui fait foi.*

**État : implémenté et testé** (milestone 10).

## Forme

`CyGPUInspectorMCP.exe` est un **proxy stdio** : un agent externe le lance, il parle JSON-RPC (MCP)
sur stdin/stdout et relaie vers le standalone par le named pipe `\\.\pipe\CyGPUInspector\app`.

Ce découpage est volontaire. Le proxy **ne détient aucune donnée et ne décide rien** : il tourne
dans le processus où un agent l'a lancé, donc il ne peut pas être chargé de se surveiller
lui-même. Le standalone possède la session et le niveau de permission.

Les requêtes sont traitées **sur le thread d'interface** du standalone, pas sur le thread du pipe :
elles lisent le modèle, le frame graph et le cache d'analyse, et y répondre depuis un autre thread
reviendrait à verrouiller tout cela de l'extérieur. Une frame de latence est sans importance pour
un appel d'outil.

Le transport est du JSON-RPC, **un objet par ligne**. Méthodes supportées : `initialize`,
`tools/list`, `tools/call`, `ping`, plus les notifications, ignorées. `initialize` renvoie des
`instructions` qui disent à l'agent par où commencer et le préviennent que les noms de passe sont
déduits et que le HLSL est une reconstruction.

## Niveaux de permission

Trois niveaux, `Read Only` par défaut, modifiables **uniquement dans l'interface du standalone** :

| Niveau | Outils ajoutés |
|---|---|
| `Read Only` | `get_current_frame`, `list_shaders`, `inspect_shader`, `get_shader_disassembly`, `get_shader_decompiled_hlsl`, `decompile_shader`, `get_shader_draw_calls`, `get_shader_resources`, `get_shader_timing`, `list_resources`, `inspect_resource`, `get_resource_readers`, `get_resource_writers`, `get_frame_graph`, `get_passes`, `inspect_draw`, `inspect_dispatch`, `search_shaders`, `search_resources` |
| `Debug Control` | `disable_shader`, `enable_shader`, `highlight_shader`, `capture_frame` |
| `Shader Modification` | `compile_shader`, `replace_shader`, `restore_shader` |

Chaque appel d'un niveau supérieur à `Read Only` est journalisé et visible dans le standalone.

## Pourquoi ces outils

Le but n'est pas de donner à une IA un gros document à deviner, mais de lui laisser **parcourir les
données elle-même**. Un workflow typique pour « trouve la passe Bloom » :

```
get_frame_graph()
 → repérer les ressources demi-résolution
 → find shaders reading the HDR SceneColor
 → suivre la chaîne de downsample
 → identifier les passes de flou et de composition
 → répondre avec passe, shaders, ressources, timings et raisonnement
```

Puis, avec la permission `Debug Control`, `disable_shader(...)` confirme ou infirme la conclusion en
regardant le jeu.

## Réponses

Chaque outil renvoie des données structurées et **sourcées** : identifiants d'événements, de
shaders et de ressources qui permettent de revenir à la commande exacte. Une réponse ne contient
jamais une classification sans sa confiance et sans les faits sur lesquels elle repose.

Certaines réponses portent un champ `caveat` explicite, par exemple :

* `get_frame_graph` : « aucun jeu ne nomme ses passes, ces noms sont déduits » ;
* `get_shader_decompiled_hlsl` : « ce sont des reconstructions, pas le source original » ;
* `get_shader_timing` : « le temps d'une commande est l'intervalle jusqu'à la suivante ».

## Limites

* Un outil qui demande un travail long (désassemblage, décompilation) le **démarre** et répond
  « redemande dans un instant » plutôt que de bloquer l'interface.
* Le pipe n'accepte qu'un proxy à la fois.
* `get_shader_timing` exige un niveau de tracking qui profile ; il le dit au lieu de renvoyer zéro.
