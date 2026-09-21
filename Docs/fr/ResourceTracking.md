# Suivi des ressources

*Disponible aussi en [anglais](../ResourceTracking.md), qui fait foi.*

**État : implémenté (milestone 1).**

## Identifiants

Chaque ressource créée par le jeu reçoit un identifiant de session stable, croissant, affiché tel
quel dans l'interface (`Resource #183`). Un identifiant n'est jamais réutilisé : à la destruction,
l'enregistrement reste et est marqué non vivant, parce que les événements des frames précédentes le
référencent encore.

Sont suivis : buffers, textures 1D/2D/3D, surfaces — donc aussi les constant buffers, vertex
buffers, index buffers et structured buffers, pas seulement les render targets.

## Métadonnées

`kind`, `format`, `width`, `height`, `depth_or_layers`, `mip_levels`, `samples`, `usage_flags`,
`buffer_size`, `created_frame`, `destroyed_frame`, handle natif.

Les formats sont transportés en `uint32_t` avec les valeurs de `reshade::api::format`, qui sont
compatibles `DXGI_FORMAT`. Le standalone n'a donc jamais besoin d'inclure le SDK ReShade. La table
de noms (`FormatNames.inc`, 131 formats) est **générée** depuis l'en-tête du SDK par
`Tools/generate_format_names.py` : elle ne peut pas diverger silencieusement.

`usage_flags` est une traduction de `resource_usage` : render target, depth stencil, shader
resource, UAV, index / vertex / constant buffer, argument indirect, source / destination de copie ou
de résolution, plus deux drapeaux à nous : back buffer et partagé.

Les back buffers sont identifiés à `init_swapchain` en parcourant `get_back_buffer(i)`.

## Vues

`init_resource_view` alimente la table `vue → ressource`. C'est indispensable : les bindings de
render target, les clears et `generate_mipmaps` travaillent sur des vues, pas sur des ressources.
Sans cette table, on ne saurait pas *quelle* texture un draw écrit.

## Accès par frame

Chaque événement porte jusqu'à deux ressources :

| Champ | Contenu |
|---|---|
| `primary_resource` | ce qui est **écrit** : render target 0, destination de copie ou de résolution, ressource effacée |
| `secondary_resource` | ce qui est **lu** ou la cible de profondeur : source de copie, depth target |

Le standalone en dérive par frame : écritures, lectures, premier et dernier événement d'écriture,
total cumulé. C'est déjà la matière première du graphe de dépendances (§7) : chaque arête porte
l'index de l'événement qui l'a produite.

## Limites actuelles

* Seule la render target 0 est enregistrée par événement. Les MRT complets demandent un record de
  bindings séparé, prévu pour le mode `Capture` (milestone 4).
* Les lectures par SRV ne sont pas encore suivies : elles passent par `push_descriptors` /
  `bind_descriptor_tables`, les événements les plus coûteux de l'API, réservés au mode capture.
  Aujourd'hui « lecture » signifie source de copie ou cible de profondeur.
* Les noms de debug des ressources (`set_resource_name`) sont rarement fournis par les jeux ; le
  record `resource_named` existe dans le protocole mais n'est pas encore émis.
