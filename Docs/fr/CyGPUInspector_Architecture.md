# CyGPUInspector — Architecture

*Disponible aussi en [anglais](../CyGPUInspector_Architecture.md), qui fait foi.*

Version 0.1.0 — document écrit **avant** l'implémentation lourde, comme demandé (§71 du cahier
des charges). Il est la référence : tout écart d'implémentation doit d'abord être corrigé ici.

Licence du projet : **GPLv3**. Ce choix autorise l'intégration directe de composants GPL
(décompilateur 3Dmigoto), à condition de conserver copyrights, notices et sources.

---

## 1. Vue d'ensemble

```
PROCESSUS DU JEU                                   PROCESSUS STANDALONE
─────────────────────────────────────              ─────────────────────────────────────
Game (D3D11 / D3D12)
   ↓  appels API
ReShade 6.8 (build add-on, API 20)
   ↓  événements reshade::api
CyGPUInspectorRS.addon64                           CyGPUInspectorApp.exe
 ├── ShaderTracker    bytecode → signature          ├── Session / Connection manager
 ├── ResourceTracker  ressources + accès            ├── Frame model (events, shaders, res.)
 ├── FrameRecorder    flux d'événements             ├── Frame graph builder
 ├── GpuTimer         query heaps timestamp         ├── Resource preview (D3D11 + ImGui)
 ├── RuntimeControl   disable / highlight / replace ├── Shader disassembly + decompile
 ├── PreviewBridge    copie → texture partagée      ├── Search engine
 └── Overlay          mini panneau ReShade          ├── Database (SQLite + blobs)
                                                    └── Serveur de contrôle (pipe local)
        │                                                        ▲
        │  ① métadonnées : ring buffer en mémoire partagée       │  ③ JSON-RPC stdio
        │  ② contrôle    : named pipe (requête/réponse)          │
        │  ④ pixels      : textures GPU partagées (handles)      │
        └────────────────────────────────────────────────────────┘
                                                    CyGPUInspectorMCP.exe (proxy MCP)
```

Quatre canaux distincts, pour quatre besoins qui n'ont pas les mêmes contraintes :

| # | Canal | Transport | Sens | Contrainte |
|---|---|---|---|---|
| ① | Événements de frame | ring buffer SPSC en mémoire partagée | RS → App | jamais bloquant côté jeu |
| ② | Commandes / requêtes | named pipe en mode message | App → RS (+ réponse) | fiable, ordonné, rare |
| ③ | MCP | JSON-RPC sur stdio, relayé par named pipe | Agent IA → App | hors chemin critique |
| ④ | Pixels | textures D3D partagées + fence | RS → App | zéro readback CPU |

---

## 2. Découpage en composants

| Composant | Nature | Contenu |
|---|---|---|
| `CyGPUInspectorCore` | bibliothèque statique C++17, sans dépendance | protocole IPC, structures partagées, SHA-256, ring buffer, annuaire de sessions, utilitaires de format |
| `CyGPUInspectorRS` | `.addon64` chargé par ReShade | tracking, capture, contrôle runtime, overlay minimal |
| `CyGPUInspectorApp` | exe Win32 + D3D11 + Dear ImGui | toute l'analyse, la visualisation, l'édition |
| `CyGPUInspectorDecompiler` | bibliothèque statique | désassemblage, backends de décompilation, validation |
| `CyGPUInspectorDatabase` | bibliothèque statique | SQLite + magasin de blobs, profils de jeu, cache |
| `CyGPUInspectorMCP` | petit exe stdio | serveur MCP, proxy vers l'App |
| `CyGPUInjector` | add-on ReShade | applique un mod exporté : remplace des shaders, supprime des draws. Aucun IPC, aucune analyse |
| `ThirdParty` | — | reshade (BSD-3), imgui (MIT), sqlite (domaine public), 3Dmigoto decompiler (GPL-3), dxil-spirv (MIT), SPIRV-Cross (Apache-2.0) |

Règle structurante : **l'add-on ne fait qu'observer, transmettre et exécuter des ordres.**
Il ne contient ni base de données, ni décompilateur, ni analyse, ni interface complexe.

---

## 3. CyGPUInspectorRS — l'agent dans le jeu

### 3.1 État par device

Attaché via `device->set_private_data<DeviceContext>()` :

```
DeviceContext
├── api, adapter, capacités (shared_resource, shared_fence, timestamp queries)
├── ShaderTracker      signature → ShaderRecord ; pipeline handle → [signatures]
├── ResourceTracker    resource handle → ResourceRecord ; view handle → resource handle
├── FrameRecorder      événements de la frame en cours + compteurs
├── GpuTimer           query heaps timestamp, résultats décalés de N frames
├── RuntimeControl     ensembles « disabled », « highlighted », remplacements actifs
├── PreviewBridge      requêtes de preview → textures partagées
└── IpcServer          ring buffer + named pipe
```

Par command list (`set_private_data<CommandListState>`) : bindings courants (RT, DSV, pipeline
par étage, index/vertex buffers, descripteurs si capture active) et un tampon local
d'événements. **Aucun verrou** : une command list n'est enregistrée que par un thread à la fois.
Le tampon est fusionné dans le `FrameRecorder` sur `execute_command_list` (D3D12, contextes
différés) ou sur `present` (contexte immédiat D3D11).

### 3.2 Niveaux d'activité

Le coût de l'outil est piloté par un niveau global, changeable à chaud depuis l'App :

| Niveau | Événements enregistrés | Coût visé |
|---|---|---|
| `Idle` | `init/destroy_*` uniquement | quasi nul |
| `Tracking` (défaut) | + `create/init_pipeline`, `bind_pipeline`, RT/DSV, draw, dispatch, clear, copy | < 2 % CPU |
| `PassTiming` | + timestamps par passe détectée | < 5 % |
| `Capture` (une frame) | + descripteurs, viewports, états, buffers, constantes | pic accepté sur 1 frame |
| `FullDrawTiming` | + timestamp autour de chaque draw | mode diagnostic, explicitement activé |

L'enregistrement/désenregistrement des callbacks ReShade est effectif : passer en `Idle` retire
réellement les callbacks coûteux (`reshade::unregister_event`).

### 3.3 Signature de shader

À `create_pipeline` / `init_pipeline`, pour chaque sous-objet de type shader :

```
signature      = SHA-256( bytecode complet, champ checksum du conteneur DXBC mis à zéro )
semantic_hash  = SHA-256( parties de code uniquement : SHEX/SHDR pour DXBC, DXIL pour SM6 )
```

* `signature` identifie l'octet près : c'est la clé de la base et des remplacements.
* `semantic_hash` regroupe les variantes qui ne diffèrent que par des parties de debug ou des
  métadonnées : c'est la clé utilisée pour « j'ai déjà vu ce shader dans une autre build ».

Le bytecode est copié **dans le callback** (le pointeur n'est pas valide après) puis envoyé une
seule fois à l'App, qui le persiste. Les frames suivantes ne transportent que la signature.

### 3.4 Flux d'une frame

1. `bind_pipeline` → la command list mémorise la signature par étage (lookup O(1)).
2. `bind_render_targets_and_depth_stencil` / `begin_render_pass` → RT/DSV courants.
3. `draw*` / `dispatch*` → un `DrawEvent` compact (indices vers les tables, pas de chaînes) est
   poussé dans le tampon local ; si `RuntimeControl` marque le shader comme désactivé, le
   callback retourne `true` (commande supprimée).
4. `copy_*` / `clear_*` / `resolve_*` / `barrier` → événements de lecture/écriture de ressources.
5. `present` → fusion des tampons, clôture de la frame, envoi du bloc d'événements dans le ring,
   lecture des résultats de timestamps de la frame N-3, traitement des commandes reçues.

Les événements sont des structures POD de taille fixe (voir `Protocol.hpp`). Une frame typique
de 5 000 draws produit ~400 Ko : le ring de 64 Mo absorbe plus de 100 frames de retard.

### 3.5 Règle de non-blocage

Si le ring est plein, l'add-on **abandonne** les événements de la frame et incrémente un
compteur `dropped_frames` transmis dans l'en-tête de la frame suivante. Il ne bloque jamais, ne
réalloue jamais dans le chemin chaud, et n'attend jamais l'App. Les données non rejouables
(bytecode d'un nouveau shader) sont mises dans une file de renvoi et retentées à la frame
suivante.

---

## 4. IPC — détails

### 4.1 Annuaire des sessions

Un file mapping nommé `Local\CyGPUInspector.Sessions.v1` (créé par le premier arrivé) contient
un en-tête + 32 slots :

```
SessionSlot { pid, api, process_name[64], ring_name[64], pipe_name[64],
              protocol_version, start_time, heartbeat_qpc, flags }
```

L'add-on réclame un slot par `InterlockedCompareExchange` sur `pid`, et met à jour
`heartbeat_qpc` à chaque present. L'App liste les slots, vérifie la fraîcheur du heartbeat **et**
l'existence du processus (`OpenProcess`), et affiche la liste des applications connectées (§64).
Un slot dont le processus est mort est recyclé. Plusieurs jeux simultanés sont donc supportés
nativement (§65).

### 4.2 Ring buffer d'événements

`Local\CyGPUInspectorRS.<pid>.events` — SPSC sans verrou :

```
RingHeader { magic, version, capacity, write_pos (atomic), read_pos (atomic),
             dropped_bytes, sequence }
Record     { size, type, sequence } + charge utile alignée sur 8 octets
```

Producteur unique (le thread de present), consommateur unique (le thread IPC de l'App). Un
`Event` Win32 auto-reset signale l'arrivée de données pour éviter le polling actif côté App.

### 4.3 Canal de contrôle

Named pipe `\\.\pipe\CyGPUInspector\<pid>` en mode message, un seul client à la fois.
Requêtes sérialisées en structures POD (pas de JSON dans le jeu) :

`Hello(app_pid, protocol) → HelloAck(caps)`, `SetLevel`, `CaptureFrame`,
`RequestPreview(resource_id, mip, slice, channel_mode)`, `DisableShader`, `EnableShader`,
`HighlightShader`, `ReplaceShader(signature, bytecode)`, `RestoreShader`, `GetResourceBlob`,
`SetTimingMode`, `Ping`.

`Hello` transmet le PID de l'App : indispensable pour `DuplicateHandle` des handles NT (§5).

### 4.4 Robustesse (§66)

* Chaque canal est indépendant : la mort de l'App ferme le pipe, l'add-on repasse en `Tracking`
  et continue à tourner. Aucun `WaitForSingleObject` infini côté jeu.
* Toute écriture dans le ring est bornée ; toute lecture côté App valide `size` avant de
  déréférencer (le jeu peut avoir été tué en plein écrit).
* Un `__try/__except` autour du traitement d'événements dans l'add-on transforme une exception
  en désactivation du tracking, jamais en crash du jeu.
* À la reconnexion, l'App redemande un `FullSync` : tables de shaders et de ressources
  retransmises.

---

## 5. Partage GPU des previews (§10, §12)

```
Ressource du jeu ──(copy_texture_region sur la queue du jeu)──▶ Texture partagée (RS)
                                                                      │ handle
                                                                      ▼
                                       CyGPUInspectorApp : OpenSharedResource → SRV → ImGui::Image
```

* La texture partagée est créée par l'add-on avec `resource_flags::shared` (D3D11) ou
  `shared_nt_handle` (D3D12), en un seul mip et une seule couche, dans un format **copy compatible
  avec la source** : les formats de profondeur passent par leur famille typeless, qui est à la fois
  partageable et visible comme shader resource. Voir `Docs/GPUSharing.md` pour la table exacte.
* La conversion d'affichage (canal, profondeur linéarisée, reverse-Z, mise à l'échelle) se fera
  côté standalone, qui possède déjà un device D3D11 et la vue : l'add-on n'embarque aucun shader.
* Synchronisation : `device::create_fence(..., shared_handle)` quand `device_caps::shared_fence`
  est disponible — l'add-on signale la valeur N après la copie, l'App attend N avant de lire.
  Sans fence partagée, on utilise deux textures en alternance plus un compteur de frame dans la
  mémoire partagée : le déchirement résiduel est visuellement acceptable pour une preview et est
  documenté comme tel.
* Handles NT : `DuplicateHandle` depuis le processus du jeu vers celui de l'App (PID reçu au
  `Hello`). Handles legacy D3D11 : directement ouvrables, pas de duplication.
* **Aucun readback CPU dans le flux live.** Le readback (`copy_texture_to_buffer` + map) n'est
  utilisé que pour : sauvegarde d'une ressource, export, capture de frame offline, analyse d'un
  buffer structuré. Ces opérations sont ponctuelles et explicites.

---

## 6. Modèle de données (côté App)

```
Session        (processus connecté ou capture ouverte)
 ├── Shader     signature, stage, api, shader_model, taille, first_seen, last_seen, tags
 ├── Pipeline   handle, [signatures], layout, formats RT/DSV, états
 ├── Resource   id, desc complet, événements de création/destruction, compteurs R/W
 ├── Frame      index, durée, [Event]
 │    ├── DrawEvent      pipeline, shaders, RT/DSV, counts, timing, bindings (si capture)
 │    ├── DispatchEvent  groupes, CS, SRV/UAV/CBV, timing
 │    └── ResourceEvent  copy / clear / resolve / barrier / map
 ├── Pass       cluster d'événements (dérivé), nom, timing agrégé
 └── Graphs     graphe de passes, graphe de dépendance de ressources
```

Toutes les listes de l'UI sont virtualisées (`ImGuiListClipper`) et indexées en tâche de fond :
50 000 draw calls et 3 000 shaders sont un cas nominal, pas un cas limite (§61).

---

## 7. Frame graph dérivé (§6, §7)

Aucun jeu ne fournit de noms de passe (cf. `Docs/Research/ReShadeAddonAPI.md` §7). Le graphe est
donc **reconstruit** en trois étapes :

1. **Clustering** — des événements consécutifs forment une passe quand ils partagent le même jeu
   de render targets + depth target et la même classe de pipeline. Un changement de RT, un clear,
   un dispatch ou une barrière ferme la passe.
2. **Dépendances** — pour chaque ressource : producteurs (draw/dispatch/copy/clear qui écrivent),
   consommateurs (lectures via SRV, copies sources). Les arêtes passe→passe viennent de
   « la passe B lit une ressource écrite par la passe A ».
3. **Classification heuristique** — appliquée sur des invariants, avec un score de confiance :
   * profondeur seule, pas de RT couleur, tôt dans la frame → *Depth Prepass*
   * ≥ 3 RT simultanés, formats normal/albédo/roughness → *GBuffer*
   * compute lisant depth + normales, écrivant un UAV pleine résolution → *Lighting* / *AO*
   * chaîne de RT HDR à résolution divisée par deux successivement → *Bloom downsample*
   * passe unique lisant un RT HDR et écrivant un RT LDR juste avant l'UI → *Tonemap*
   * petits draws alpha-blend écrivant le back buffer en fin de frame → *UI*
   Tout le reste : `Unknown Pass #N`. Les noms peuvent être imposés par l'utilisateur ou proposés
   par l'IA, et l'origine du nom (`user` / `heuristic` / `ai`) est toujours affichée (§25, §40).

Le graphe de dépendance des ressources répond exactement aux questions de §7 (créée par, écrite
par, lue par, copiée de/vers, résolue de/vers, détruite) car chaque arête porte l'index
d'événement qui l'a produite.

---

## 8. Profiling GPU (§22–§25)

* `query_heap(timestamp)` alloué par frame en anneau (3 frames en vol), lecture décalée pour ne
  jamais attendre le GPU.
* `command_queue::get_timestamp_frequency()` convertit les ticks en millisecondes.
* Niveaux : passe / shader sélectionné / draw sélectionné / agrégation par shader / full draw.
* En `FullDrawTiming`, deux timestamps par draw : c'est un mode diagnostic qui peut diviser le
  framerate par deux, annoncé comme tel dans l'UI (§23, §67).
* Agrégation par signature de shader : appels, total, moyenne, max (§24).
* Le coût de l'outil lui-même est mesuré et affiché (§68) : temps CPU passé dans les callbacks,
  octets/s dans le ring, VRAM des textures partagées.

---

## 9. Base persistante (§17, §57, §62, §63)

```
Database/
├── cygpuinspector.db            SQLite : shaders, tags, relations, captures, analyses
├── shaders/<aa>/<signature>/    blobs (bytecode, désassemblage, décompilations, analyses)
│   ├── metadata.json
│   ├── original.dxbc | original.dxil
│   ├── disassembly.txt
│   ├── decompiled/<backend>-<version>.hlsl     ← un fichier par backend, jamais écrasé
│   ├── analysis.md
│   ├── tags.json
│   └── replacements/<name>.hlsl + .cso
└── Games/<Executable>/          profil par jeu : shaders connus, tags, captures, remplacements
```

SQLite pour les métadonnées et les requêtes, disque pour les gros blobs. Toute opération coûteuse
(désassemblage, décompilation, nettoyage IA, classification) est mise en cache par signature, avec
`{ outil, version, date, modèle IA, version du prompt }` afin de pouvoir régénérer plus tard
(§58). Un shader déjà connu d'un autre lancement est reconnu par `signature`, sinon rapproché par
`semantic_hash`.

---

## 10. Contrôle runtime (§33–§38)

| Action | Mécanisme |
|---|---|
| `Disable` (shader) | l'add-on retourne `true` sur les draws/dispatch dont le pipeline lié contient la signature |
| `Disable draw/dispatch` | même mécanisme, filtré sur un identifiant d'événement précis |
| `Highlight` | remplacement du pixel shader par un shader magenta généré, ou atténuation du reste |
| `Replace` | compilation côté App (DXC/FXC) → bytecode envoyé par pipe → création d'un pipeline de remplacement → échange au `bind_pipeline` |
| `Restore` | destruction du pipeline de remplacement, retour au pipeline original |

L'état (`disabled`, `highlighted`, tag, nom, remplacement) est persisté **par signature** dans le
profil du jeu et réappliqué au lancement suivant (§38).

---

## 11. Standalone — UI (§59–§61)

Dear ImGui (docking) sur Win32 + D3D11. Motifs retenus :

* Layout par docking : Frame Graph à gauche, preview au centre, propriétés à droite, onglets
  (Disassembly | HLSL | AI Analysis | Resources | Draw Calls) en bas.
* Listes virtualisées, index construits dans un thread de fond, accès base asynchrone,
  chargement paresseux des blobs.
* Le rendu D3D11 de l'App sert aussi à afficher les textures partagées du jeu : un seul device,
  pas de copie CPU.
* Multi-écran : plusieurs fenêtres via les viewports ImGui.

Justification du choix (§60) : l'App doit ouvrir des textures D3D partagées et afficher des
listes de dizaines de milliers de lignes ; ImGui+D3D11 donne les deux sans couche d'interop, en
C++, sans installation (Qt absent de la machine, Avalonia/WinUI imposeraient un pont C++/C# pour
le cœur natif). Coût accepté : widgets à écrire nous-mêmes (éditeur de code, graphe).

---

## 12. MCP (§48–§53)

`CyGPUInspectorMCP.exe` est un **proxy stdio** : un agent externe le lance, il parle JSON-RPC MCP
sur stdin/stdout et relaie vers l'App par named pipe `\\.\pipe\CyGPUInspector\app`. L'App reste
la source de vérité, y compris pour les permissions.

Trois niveaux, `Read Only` par défaut, changeables uniquement dans l'UI de l'App :

| Niveau | Outils ajoutés |
|---|---|
| `Read Only` | `get_current_frame`, `list_shaders`, `inspect_shader`, `get_shader_disassembly`, `get_shader_decompiled_hlsl`, `decompile_shader`, `get_shader_draw_calls`, `get_shader_resources`, `get_shader_timing`, `list_resources`, `inspect_resource`, `get_resource_readers`, `get_resource_writers`, `get_frame_graph`, `get_passes`, `inspect_draw`, `inspect_dispatch`, `search_shaders`, `search_resources` |
| `Debug Control` | `disable_shader`, `enable_shader`, `highlight_shader`, `capture_frame` |
| `Shader Modification` | `compile_shader`, `replace_shader`, `restore_shader` |

Chaque appel d'un niveau élevé est journalisé et visible dans l'App.

---

## 13. IA (§30, §31, §40, §56)

L'App n'embarque aucun modèle. Deux chemins :

1. **Agent externe via MCP** — le chemin principal : l'IA navigue elle-même dans les données.
2. **Jobs assistés** — nettoyage de HLSL, classification, explication, déclenchés depuis l'UI avec
   une clé API fournie par l'utilisateur, exécutés en tâche de fond, résultats mis en cache.

Règles non négociables :

* Toute sortie IA est étiquetée `AI reconstructed` et distinguée de `Decompiler reconstructed`,
  elles-mêmes distinguées de `Original source` (jamais disponible) — §31.
* Une classification IA est toujours accompagnée d'une confiance et présentée comme hypothèse,
  jamais comme fait (§40).
* L'IA travaille **sur des données concrètes** (désassemblage, bindings, graphe, timings)
  extraites par l'outil ; le snapshot d'analyse (§56) est le paquet exact qu'on lui donne.

---

## 14. Captures offline (§41–§45)

```
Captures/<Game>_2026-09-16_23-45/
├── frame.json          en-tête, compteurs, versions d'outils
├── events.bin          flux d'événements de la frame (même format que l'IPC)
├── shaders/            signatures présentes (référence la base, ne duplique pas les blobs)
├── resources/          descriptions + previews PNG/DDS exportées
├── timings/
├── graph.json          frame graph et graphe de ressources dérivés
└── analysis/           notes utilisateur et analyses IA
```

Le point de conception le plus utile : **le chargement ne désérialise pas dans le modèle**. Il
reconstruit les mêmes records IPC que l'add-on aurait envoyés et les passe à
`SessionModel::ApplyRecord`. Une capture ouverte traverse donc exactement le même code qu'une
session vivante, et il n'y a pas de second désérialiseur à maintenir en phase.

Une capture ouverte sans jeu donne accès à tout sauf aux actions runtime, qui sont désactivées
(§43) : le frame graph est reconstruit, les shaders se désassemblent et se décompilent, la
recherche et le MCP fonctionnent. Seules les previews GPU disparaissent, parce qu'une capture ne
garde pas de pixels. Deux captures se comparent (§44, §45) : draws, dispatches, événements,
shaders (par signature, pas par identifiant, qui est local à une session), ressources et temps GPU.

---

## 15. Compatibilité et limites assumées (§3)

* L'outil ne fonctionne **que** là où ReShade fonctionne normalement. Aucun contournement
  d'anti-triche, de protection ou de blocage d'injection n'est développé. Une application qui
  refuse ReShade est simplement **non supportée**.
* D3D11 et D3D12 d'abord. Vulkan et OpenGL sont prévus par l'architecture (l'API ReShade est déjà
  abstraite) mais n'ont pas de backend de décompilation au départ.
* Les limites connues de chaque sous-système sont centralisées dans `Docs/Limitations.md` et
  visibles dans l'UI plutôt que cachées.

---

## 16. Milestones

| # | Contenu | État |
|---|---|---|
| 1 | RS charge, détecte l'API, suit shaders/ressources/draws, envoie les métadonnées | en cours |
| 2 | App standalone : connexions, listes shaders / ressources / draws | en cours |
| 3 | Previews GPU partagées (RT, depth) | fait |
| 4 | Frame graph + graphe de ressources | fait |
| 5 | Désassemblage DXBC/DXIL avec cache | fait (+ réflexion, + compilation) |
| 6 | Décompilation multi-backend + validation | fait (1 backend livré) |
| 7 | Contrôle runtime (disable / highlight / replace / restore) | fait |
| 8 | Profiling GPU (passe → shader → draw → full) | fait |
| 9 | IA (étiquetage, tags, annotations, snapshot) | fait ; jobs API à faire |
| 10 | Serveur MCP (read-only puis debug puis modification) | fait |
| 11 | Captures offline, ouverture, comparaison | fait |
