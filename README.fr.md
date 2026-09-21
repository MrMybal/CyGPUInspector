<p align="center"><img src="Assets/cygpuinspector-logo-256.png" alt="Logo de CyGPUInspector" width="160"></p>

# CyGPUInspector

*Disponible aussi en [anglais](README.md), qui est la langue de référence du projet.*

Inspecteur GPU temps réel et offline : frame debugger, inspecteur de shaders et de ressources,
profileur, décompilateur et analyse assistée par IA, avec **ReShade comme agent de capture et de
contrôle** dans le processus du jeu.

L'outil se situe entre ReShade, RenderDoc, ShaderToggler et un décompilateur de shaders. Le
composant injecté reste léger ; toute la puissance d'analyse est dans l'application standalone.

Licence : **GNU Affero General Public License v3 ou ultérieure** (AGPL-3.0-or-later). Voir [LICENSE](LICENSE).

## Composants

| Composant | Rôle | État |
|---|---|---|
| `CyGPUInspectorCore` | protocole IPC, ring buffer partagé, SHA-256, conteneurs DXBC/DXIL, formats, traduction | ✅ |
| `CyGPUInspectorRS` | add-on ReShade (`.addon64`) : tracking, capture, contrôle, timings | ✅ |
| `CyGPUInjector` | add-on ReShade (`.addon64`) qui applique un mod exporté, sans le reste de l'outil | ✅ |
| `CyGPUInspectorApp` | standalone Win32 + D3D11 + Dear ImGui : toute l'analyse | ✅ milestones 2–11 |
| `CyGPUInspectorDecompiler` | désassemblage, réflexion, compilation, décompilation (4 backends) | ✅ milestones 5–6 |
| `CyGPUInspectorDatabase` | blobs, tags et annotations par signature ; SQLite à venir | ✅ partiel |
| `CyGPUInspectorMCP` | serveur MCP local (lecture seule par défaut) | ✅ milestone 10 |
| `CyGPUInspectorUnreal` | plugin Unreal Engine (5.3+) : captures depuis l'éditeur et depuis les builds Development / DebugGame | ✅ première version |

## Ce qui fonctionne aujourd'hui

```
Jeu (D3D11 / D3D12)
 → ReShade 6.8 (build add-on, API 20)
 → CyGPUInspectorRS.addon64
     · shaders suivis, bytecode capturé, signature SHA-256 persistante
     · pipelines, ressources, vues, render targets
     · draws, dispatches, copies, clears, résolutions, render passes
     · disable / enable d'un shader (les draws concernés sont supprimés)
     · remplacement de shader à chaud : pipeline reconstruit et échangé au moment du draw
     · timestamps GPU par passe ou par draw, lus trois frames plus tard sans jamais attendre
     · capture profonde à la demande : chaque descripteur de chaque commande, l'état fixe de
       chaque pipeline, les barrières — quelques frames, puis elle se désarme seule
     · preview GPU : copie d'une ressource dans une texture partagée, sans readback CPU
     · les buffers d'une capture profonde : chaque texture dans laquelle la frame capturée a
       écrit, copiée sur le GPU pour que le standalone l'enregistre
 → ring buffer en mémoire partagée + named pipe de contrôle
 → CyGPUInspectorApp
     · liste des applications connectées (plusieurs jeux en parallèle)
     · shaders, ressources, événements de la frame, détails, corrélations
     · preview d'une render target ou d'une profondeur : canal RGB/R/G/B/A, profondeur
       linéarisée, reverse-Z, plage réglable, mip et slice au choix
     · frame graph reconstruit : passes déduites, dépendances entre ressources, renommage
     · timeline de frame : les passes en barres à l'échelle du temps GPU, plus l'historique des
       dernières centaines de frames — c'est la capture runtime, on la laisse tourner
     · panneau de capture profonde : les liaisons par registre, l'état du pipeline, les barrières
     · chaque capture profonde enregistrée dans son propre dossier : l'image finale, et chaque
       buffer de la frame en PNG à regarder et en DDS avec les données, listés dans capture.json
     · désassemblage DXBC et DXIL, réflexion des bindings, cache persistant par signature
     · décompilation en HLSL par quatre backends, validée par recompilation
     · édition du HLSL, compilation et injection dans le jeu, highlight magenta
     · temps GPU par commande, par passe et par shader
     · tags, noms et annotations persistants par signature, chacun étiqueté par son origine
     · captures offline : sauvegarde, réouverture sans le jeu, comparaison de deux frames
     · snapshot d'analyse IA : tout ce qu'un modèle doit lire, dans un répertoire lisible
     · export d'un mod : ce qu'on a remplacé ou désactivé, écrit dans un dossier .cygimod
     · serveur MCP local, lecture seule par défaut, chaque appel journalisé
     · coût mesuré de l'outil (CPU add-on, débit IPC, pertes)
```

**Partager une modification** — ce qu'on change dans un jeu avec l'inspecteur s'exporte en un
dossier `.cygimod` que **CyGPUInjector.addon64** rejoue chez n'importe qui, sans standalone ni
IPC : un shader remplacé par le vôtre, un autre supprimé. L'appariement se fait par hash du code
du shader, donc ça survit au redémarrage et ça cesse proprement de s'appliquer quand le jeu change
le shader. Voir [Docs/fr/ModPackages.md](Docs/fr/ModPackages.md).

**Les deux captures** — une runtime, continue et peu coûteuse, et une profonde, ponctuelle et
complète — sont décrites dans [Docs/fr/CaptureModes.md](Docs/fr/CaptureModes.md), y compris ce que ni
l'une ni l'autre ne peut faire : rejouer la frame. C'est ce qui sépare CyGPUInspector d'un
RenderDoc, et c'est une conséquence directe du choix de passer par l'API add-on officielle de
ReShade plutôt que par une injection maison.

**Dans Unreal** — le plugin `CyGPUInspectorUnreal/CyGPUInspector` capture l'éditeur, et les builds
Development ou DebugGame du jeu, depuis un bouton de la barre d'outils, une commande console ou un
nœud Blueprint. Il charge ReShade (la version avec support complet des add-ons) et l'add-on avant
que le moteur de rendu démarre, comme le plugin RenderDoc d'Unreal charge RenderDoc, lance le
standalone si besoin, et les buffers capturés portent les noms qu'Unreal donne à ses render
targets. Voir [Docs/fr/UnrealPlugin.md](Docs/fr/UnrealPlugin.md).

Ce qui n'y est pas encore : les jobs IA connectés à une API (le MCP couvre le chemin principal),
SQLite pour les métadonnées, les profils par jeu, Vulkan et OpenGL.

## Langue

L'anglais est la langue de référence : le code, les commentaires, la documentation et chaque
chaîne de l'interface sont écrits en anglais d'abord. Une traduction est un catalogue dans `Lang/`
qui associe ces chaînes anglaises à une autre langue, et tout ce qu'il ne couvre pas retombe sur
l'anglais — une traduction à moitié faite ne produit donc jamais d'étiquette vide.

Le français est fourni (`Lang/fr.json`). Ajouter une langue consiste à copier `Lang/template.json`,
le remplir et le déposer dans `Lang/` ; aucune recompilation. Voir
[Docs/fr/Translating.md](Docs/fr/Translating.md).

## Brancher un agent IA

Pointer un client MCP sur `bin/Release/CyGPUInspectorMCP.exe`. Il parle MCP sur stdio et relaie
tout vers le standalone. Le niveau de permission se règle **dans le standalone**, jamais dans le
proxy : `Read Only` par défaut, puis `Debug Control` (désactiver, surligner, capturer) et
`Shader Modification` (compiler, remplacer). Chaque appel apparaît dans le panneau MCP.

## Construire

Prérequis : Windows 64 bits, Visual Studio 2022 avec les outils C++ (CMake et Ninja fournis avec).

```bash
build.cmd Release
```

Sorties dans `bin/Release/` :

| Fichier | Usage |
|---|---|
| `CyGPUInspectorRS.addon64` | à copier à côté de l'exécutable du jeu (ReShade add-on enabled) |
| `CyGPUInspectorApp.exe` | l'application d'analyse |
| `CyGPUInspectorCoreTests.exe` | auto-tests (hash, ring, DXBC, annuaire, pipe) |
| `CyGPUInspectorShaderTests.exe` | chaîne de shaders sur de vrais shaders compilés, et passe d'affichage |
| `CyGPUInspectorMCP.exe` | serveur MCP à lancer depuis un client MCP |
| `CyGPUInspectorFakeSession.exe` | add-on synthétique, pour essayer l'app sans jeu |
| `CyGPUInspectorIpcTests.exe` | test bout en bout de la chaîne RS → App |

## Essayer sans jeu

```bash
bin/Release/CyGPUInspectorFakeSession.exe
```

puis lancer `CyGPUInspectorApp.exe` et cliquer `Connect` sur `FakeGame.exe`. La session synthétique
produit une frame plausible (depth prepass, GBuffer, lighting, chaîne de bloom, tonemap, UI) et
**une vraie texture D3D11 partagée** : double-cliquer une ressource affiche son contenu dans le
panneau `Preview`, par le même chemin GPU que dans un vrai jeu.

## Tests

```bash
bin/Release/CyGPUInspectorCoreTests.exe
bin/Release/CyGPUInspectorShaderTests.exe
bin/Release/CyGPUInspectorIpcTests.exe
```

Aujourd'hui : 4106 + 142 + 141 vérifications, aucune en échec. Les tests compilent de vrais shaders,
partagent une vraie texture GPU entre deux processus et relisent ses pixels, reconstruisent un
frame graph complet, et décompilent un shader réel avant de vérifier qu'il se recompile en un
bytecode aux instructions identiques, sauvegardent une capture et la relisent, et pilotent le
serveur MCP sur stdio — rien n'est simulé sauf le jeu lui-même.

Pour le Shader Model 6, copier `dxcompiler.dll` et `dxil.dll` à côté de `CyGPUInspectorApp.exe`
si le Windows SDK n'est pas installé ; l'onglet `Tools` du panneau de code dit ce qui a été trouvé.

## Compatibilité

CyGPUInspector ne fonctionne **que** là où ReShade fonctionne normalement. Aucun contournement
d'anti-triche, de protection ou de blocage d'injection n'est développé. Une application qui refuse
ReShade est simplement non supportée.

## Documentation

Tout est dans [Docs/fr/](Docs/fr/) ; commencer par
[Docs/fr/CyGPUInspector_Architecture.md](Docs/fr/CyGPUInspector_Architecture.md), puis
[Docs/fr/Limitations.md](Docs/fr/Limitations.md).

## Tiers

| Bibliothèque | Licence | Usage |
|---|---|---|
| ReShade SDK (en-têtes) | BSD-3-Clause | API add-on |
| Dear ImGui | MIT | interface du standalone et overlay |
| 3Dmigoto (décompilateur HLSL) | GPL-3.0 | backend de décompilation `hlsldecompiler` |
| dxil-spirv | MIT | backend `dxil-spirv`, DXIL → SPIR-V |
| dxbc-spirv | MIT | backend `dxbc-spirv`, DXBC → SPIR-V |
| SPIRV-Cross | Apache-2.0 / MIT | backend `dxil-spirv`, SPIR-V → HLSL |

Détail complet, obligations de licence et provenance exacte : [THIRD-PARTY.md](THIRD-PARTY.md) et
les `ORIGIN.md` de chaque composant sous `ThirdParty/`.
