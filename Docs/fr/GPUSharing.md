# Partage GPU des previews

*Disponible aussi en [anglais](../GPUSharing.md), qui fait foi.*

**État : implémenté et testé bout en bout** (`Tests/CyGPUInspectorIpcTests`), avec les limites
listées au §6.

## Objectif

Afficher dans le standalone une texture du jeu **sans readback CPU** (§10 du cahier des charges).

```
Ressource du jeu ──(copie sur la queue du jeu)──▶ Texture partagée (add-on)
                                                       │ handle
                                                       ▼
                     CyGPUInspectorApp : OpenSharedResource → SRV → ImGui::Image
```

Ce que l'on ne fait **pas** pour le flux live : `GPU → readback CPU → IPC → standalone → GPU`.

## Ce qui est implémenté

* `PreviewBridge` côté add-on : une requête du standalone, une texture partagée créée sur le
  device du jeu, une copie par frame sur sa propre queue, le handle transmis une seule fois.
* `SharedTexture` côté standalone : ouverture du handle (legacy ou NT), création d'une SRV,
  affichage direct par `ImGui::Image`.
* Panneau `Preview` : ajustement à la fenêtre ou zoom, sélection du mip et de la slice, arrêt.
  Double-cliquer une ressource dans la liste lance sa preview.
* Diagnostic explicite : `PreviewStatus` dit *pourquoi* une preview est indisponible
  (ressource détruite, format sans équivalent D3D11, multi-échantillonnage, partage non supporté,
  duplication de handle refusée) au lieu d'afficher une zone noire.

Le test bout en bout crée une texture partagée dans un processus, l'ouvre dans un autre, construit
la SRV **et relit les pixels** pour vérifier qu'il s'agit bien de la même mémoire GPU.

## Création de la texture partagée

`device::create_resource(desc, nullptr, state, &resource, &shared_handle)` avec :

* `resource_flags::shared` en D3D11 → handle DXGI *legacy*, ouvrable directement par un autre
  processus sur le même adaptateur ;
* `resource_flags::shared_nt_handle` en D3D12 → handle NT, à **dupliquer** vers le processus du
  standalone avec `DuplicateHandle`. Le PID nécessaire est reçu dans `HelloRequest`.

La texture est créée en **un seul mip, une seule couche**, parce qu'un handle partagé *legacy*
D3D11 l'exige. C'est aussi exactement ce dont une preview a besoin : le mip et la slice demandés
sont copiés dans le mip 0 de la texture partagée.

### Les deux bits en D3D12, et l'usage cible de rendu

En D3D12 la texture est créée avec `resource_flags::shared | resource_flags::shared_nt_handle`.
`shared` est ce qui rend une ressource partagée ; `shared_nt_handle` dit seulement quel type de
handle. Ce sont deux bits distincts (0x2 et 0x800), et le back-end D3D12 de ReShade teste `shared`.
La première version ne passait que le bit NT : ReShade créait une texture ordinaire, non partagée,
sans handle, et aucun jeu D3D12 n'a jamais produit d'aperçu. Une texture créée sans handle est
maintenant signalée comme telle (`sharing_unsupported`) au lieu d'un échec de duplication.

La texture est aussi créée comme cible de rendu, bien que rien n'y soit jamais dessiné : une texture
partagée doit pouvoir servir de cible de rendu *et* de ressource shader, sinon D3D11 refuse de la
créer (handles legacy) ou de l'ouvrir (`E_INVALIDARG` sur `OpenSharedResource1` pour un handle NT
venu de D3D12). Les formats qui ne peuvent pas être des cibles de rendu — la famille profondeur
24/8 — ont un second essai sans cet usage, avec un avertissement dans le journal.

### L'image finale

`resource_id = kPreviewFinalImage` demande ce que présente la swap chain : l'image du jeu pour la
frame, avant les effets ReShade. L'add-on la résout au moment du present avec
`swapchain::get_current_back_buffer()`, puisque le buffer présenté change à chaque frame. C'est ce
que le standalone affiche de lui-même à la connexion, à côté des deux vues de la timeline, pour une
copie GPU du backbuffer par frame.

### Les états en D3D12

La copie doit sortir la source de l'état où elle se trouve, puis l'y remettre. Au moment du present,
un backbuffer est dans l'état `present` : c'est l'état utilisé pour l'image finale et pour toute
ressource marquée comme backbuffer. Pour les autres ressources, c'est l'état laissé par la dernière
barrière enregistrée : le callback de barrière compare chaque barrière à la ressource prévisualisée
en une seule lecture atomique, donc il ne coûte rien quand aucun aperçu ne tourne. `shader_resource`
n'est supposé que si aucune barrière n'a été vue.

### Figer, et l'image d'une capture

`freeze_preview` arrête de rafraîchir l'image partagée sans la libérer. Le standalone l'envoie quand
**Suivre** est décoché : l'image reste celle du moment où la timeline a été mise en pause.

Quand une capture profonde se termine, l'add-on copie l'image de la dernière frame capturée dans ce
même present, puis la conserve. Le standalone reconnaît la capture terminée à son numéro de première
frame, marque l'image comme celle de la frame capturée, l'enregistre en `final.png` dans le dossier
propre à la capture, à côté de l'exécutable (`Images/<jeu>_<date>_<heure>_frame<N>/`), et l'affiche dans le panneau
Capture profonde. **Revenir au direct** la relâche. Une nouvelle demande d'aperçu repart toujours en
direct : une image figée par une capture ne peut jamais retenir un nouveau lecteur sur une ancienne
image sans le dire.

### Qui ferme un handle

ReShade remet à l'add-on le handle NT d'une nouvelle texture partagée et n'en garde aucune copie.
Un handle NT ouvert maintient la mémoire de la texture en vie quoi qu'il arrive à la ressource :
l'add-on ferme donc le sien dès qu'il l'a dupliqué dans le standalone, et le standalone ferme sa
copie quand il ferme la texture — y compris quand l'ouverture a échoué. Avant cela, chaque texture
d'aperçu recréée laissait fuir sa mémoire dans le jeu jusqu'à sa fermeture : quelques mégaoctets à
chaque fois, et c'aurait été la totalité des buffers d'une capture à chaque fois.

### Les buffers d'une capture profonde

Les mêmes fonctions — créer une cible de copie partagée, en transmettre le handle — servent aux
buffers de la capture profonde : une texture partagée par texture dans laquelle la frame capturée a
écrit, copiée une fois à la fin de cette frame au lieu de chaque frame, relue une fois par le
standalone puis libérée. Voir [CaptureModes.md](CaptureModes.md#ce-quelle-enregistre-sur-le-disque).

### Enregistrer

**Enregistrer en PNG** écrit ce qui est à l'écran — canaux et plage appliqués — à la résolution
native de la texture, via le Windows Imaging Component. La relecture a lieu une seule fois, dans le
processus du standalone, à la demande : jamais dans celui du jeu, jamais dans le chemin en direct.

### La carte graphique

Une texture partagée ne s'ouvre que sur la carte qui l'a créée. Le standalone crée son device sur la
carte haute performance (`EnumAdapterByGpuPreference`), celle qu'utilisent les jeux, au lieu de la
carte « par défaut », qui sur une machine avec aussi un GPU intégré est la mauvaise. Si la carte du
jeu et celle du standalone diffèrent quand même, le panneau Prévisualisation le dit, avec les deux
noms.

### Choix du format

Une copie D3D n'est légale qu'à l'intérieur d'une même famille *typeless*, et une texture de
profondeur ne peut être ni partagée ni vue comme shader resource. `FormatShareableCopyTarget`
(dans Core) résout les trois contraintes d'un coup en partageant les formats de profondeur via
leur membre typeless :

| Source | Texture partagée | Vue côté standalone |
|---|---|---|
| `d32_float` | `r32_typeless` | `r32_float` |
| `d24_unorm_s8_uint` | `r24_g8_typeless` | `r24_unorm_x8_uint` |
| `d16_unorm` | `r16_typeless` | `r16_unorm` |
| `d32_float_s8_uint` | `r32_g8_typeless` | `r32_float_x8_uint` |
| tout le reste | le format source | le même, concrétisé s'il est typeless |

`FormatShaderResourceView` fait le chemin inverse côté standalone, en dérivant le nom concret du
nom typeless (`…_typeless` → `…_unorm` / `…_float` / …) plutôt qu'avec une table à maintenir.

## Conversion — faite côté standalone

`PreviewRenderer` applique la conversion d'affichage dans le processus du standalone, qui possède
déjà un device D3D11 et la vue sur la texture partagée. L'add-on n'embarque donc **aucun shader** :

* sélection de canal : RGB, R, G, B, A (en niveaux de gris) ;
* alpha affiché sur un damier, pour distinguer le transparent du noir ;
* profondeur brute et profondeur **linéarisée**, avec near/far et reverse-Z ;
* remise à l'échelle d'une plage de valeurs, indispensable pour un buffer HDR ou de profondeur.

Le shader de cette passe est compilé au démarrage **par notre propre `CompileHlsl`** : la chaîne
de compilation que l'outil propose à l'utilisateur est donc exercée à chaque lancement.

Une preview de profondeur démarre automatiquement en mode linéarisé, parce qu'un depth buffer brut
s'affiche en blanc uniforme et ne dit rien.

`PreviewChannels` reste dans le protocole pour le jour où une conversion devra se faire côté jeu
(par exemple pour réduire la bande passante d'une texture 8K).

## Synchronisation

* Si `device_caps::shared_fence` : `create_fence(..., &shared_handle)`, l'add-on signale la valeur
  N après la copie, le standalone attend N avant de lire. C'est la voie propre.
* Sinon : deux textures en alternance plus un compteur de frame en mémoire partagée. Le
  déchirement résiduel possible sur une preview est documenté, pas masqué par une copie CPU.

## Protocole

`PreviewRequest { request_id, resource_id, mip_level, array_slice, channels, max_width, max_height }`
sur le pipe de contrôle, `PreviewReadyRecord { request_id, resource_id, shared_handle, fence_handle,
fence_value, width, height, format, is_nt_handle }` dans le ring. Les deux structures existent déjà
dans `Protocol.hpp`.

## Limites actuelles (§6)

* **Pas de synchronisation.** Le standalone échantillonne pendant que le jeu copie : un
  déchirement est possible sur une frame. La fence partagée décrite plus haut n'est pas encore
  branchée. C'est visible, borné, et documenté plutôt que masqué par une copie CPU.
* **MSAA non supporté** : il faudrait un `resolve_texture_region`, refusé pour l'instant avec le
  statut `multisampled`.
* **`max_width` / `max_height` ignorés** : la texture est partagée à sa résolution native et mise
  à l'échelle à l'affichage.
* Les mips et les slices sont demandés au jeu (une nouvelle copie), pas échantillonnés localement.
* **États D3D12 tirés des barrières enregistrées** : exacts pour l'image finale et les backbuffers ;
  pour les autres ressources, c'est l'état de la dernière barrière *enregistrée*, qui, sur un jeu qui
  enregistre sur plusieurs threads, n'est pas toujours la dernière *exécutée*.
* Une seule preview à la fois par session.

## Quand le readback CPU est légitime

Uniquement pour des opérations ponctuelles et explicites : sauvegarde d'une ressource, export,
capture de frame offline, inspection du contenu d'un buffer structuré. Jamais dans le flux live.
