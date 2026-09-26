# CyGPUInspectorApp — le standalone

*Disponible aussi en [anglais](../StandaloneApp.md), qui fait foi.*

**État : milestones 2 à 5 implémentés.**

## Technologie

Win32 + Direct3D 11 + Dear ImGui (branche docking, MIT). Ce choix est motivé, pas par défaut :

* le standalone doit **ouvrir des textures D3D partagées** venant du jeu et les afficher sans
  copie CPU ; un device D3D11 dans le processus rend cela direct, sans couche d'interop ;
* il doit afficher des **dizaines de milliers de lignes** sans ralentir : `ImGuiListClipper` ne
  construit que les lignes visibles ;
* le cœur du projet est du C++ natif (IPC, D3D, décompilation) : Qt 6 est absent de la machine et
  impose sa propre licence, Avalonia et WinUI 3 imposeraient un pont C++/C# pour tout le cœur.

Coût accepté : les widgets riches (éditeur de code, graphe) sont à écrire nous-mêmes.

## Structure

```
CyGPUInspectorApp/Source
├── Main.cpp            fenêtre Win32, device D3D11, boucle ImGui (docking + viewports)
├── App/
│   ├── Application       panneaux, sélection, actions
│   ├── AssistantPanel    le panneau Assistant IA et l'onglet Analyse IA
│   └── HlslEditorPanel   l'onglet HLSL coloré et la fenêtre de l'éditeur HLSL
├── Assistant/
│   ├── AssistantProvider   CLI Codex, CLI Claude Code, API compatibles OpenAI et locales
│   ├── AssistantAgent      la boucle d'outils bornée et les tâches sur les shaders
│   └── Assistant           connexion, conversation, autorisations, threads de travail
├── Editor/
│   ├── HlslEditor          ImGuiColorTextEdit avec coloration HLSL et marques d'erreurs
│   └── CompilerMessages    messages de FXC et DXC rapportés aux lignes du source
├── Analysis/
│   ├── FrameGraph              clustering, dépendances, classification des passes
│   └── ShaderAnalysisService   désassemblage et réflexion sur un thread de fond, avec cache
├── Render/
│   ├── SharedTexture           ouvre la texture partagée du jeu, crée la vue
│   └── PreviewRenderer         passe d'affichage : canal, profondeur linéarisée, plage
└── Session/
    ├── SessionModel     modèle en mémoire d'une session (shaders, pipelines, ressources, frames)
    └── SessionClient    thread lecteur du ring + client du pipe de contrôle
```

`SessionModel` et `SessionClient` ne dépendent ni d'ImGui ni de D3D : c'est ce qui permet à
`Tests/CyGPUInspectorIpcTests` de tester exactement le code que l'application exécute.

## Panneaux

| Panneau | Contenu |
|---|---|
| Connections | applications détectées, connexion, niveau de tracking, capacités, resynchronisation |
| Frame timeline | l'historique des frames récentes, les passes en barres proportionnelles au temps GPU, ces mêmes passes en liste triable, et tout ce que contient celle qui est sélectionnée |
| Deep capture | armement de la capture ponctuelle, bindings par registre, état du pipeline, barrières ; l'image capturée et les buffers de la frame, enregistrés dans le dossier de la capture sous `Images/` et listés là |
| Frame graph | passes déduites, confiance, ressources écrites et lues, shaders, renommage |
| Shaders | id, étage, format, draws/frame, ms GPU ; filtre texte, filtre « cette frame ». Le shader model, la taille et la signature sont des colonnes de la même table, masquées par défaut et rappelées par son menu contextuel |
| Resources | id, type, dimensions, format, mips, écritures/frame, usage ; filtre « render targets seulement » |
| Frame events | la frame complète, virtualisée ; les événements du shader sélectionné sont surlignés, et trois filtres réduisent la liste aux dessins, aux commandes d'un shader ou à celles d'une passe |
| Preview | image GPU partagée, canal RGB/R/G/B/A, profondeur linéarisée, plage, mip, slice |
| Shader code | onglets Disassembly / Bindings / HLSL / AI Analysis / Tools. L'onglet HLSL affiche la reconstruction de chaque décompilateur colorée en HLSL, modifiable, avec Compiler, Compiler et injecter et Ouvrir dans une fenêtre |
| Assistant IA | connexion au fournisseur, tchat avec l'assistant qui lit la frame avec les outils de CyGPUInspector, mode d'autorisation. Voir [AI.md](AI.md) |
| Éditeur HLSL | une fenêtre à part (Affichage > Éditeur HLSL) : l'éditeur complet, voir ci-dessous |
| Mod export | ce qui a été remplacé ou désactivé, et le paquet vers lequel cela s'exporte |
| Captures | enregistrement, réouverture et comparaison des captures hors ligne |
| MCP | niveau de permission et journal de chaque appel |
| Details | onglets Shader / Resource / Event / Log, avec les actions runtime et le graphe de dépendances |
| Status | débit IPC, octets en attente, pertes, coût de l'add-on, compteurs côté jeu |

Sélectionner un shader surligne ses draw calls dans la frame ; sélectionner un événement
sélectionne son shader et sa render target. C'est la boucle de navigation demandée au §84. Elle
fonctionne aussi dans l'autre sens : choisir une passe, dans les barres ou dans la liste, fait
défiler Frame events jusqu'à sa première commande au lieu de laisser la liste où elle était, et
les ressources et les shaders affichés à côté des barres sont eux-mêmes la sélection.

## Éditeur HLSL

Le HLSL du shader sélectionné est un seul document, affiché soit dans l'onglet HLSL, soit dans une
fenêtre à part (**Ouvrir dans une fenêtre**, ou Affichage > Éditeur HLSL). Cette fenêtre est une
vraie fenêtre Windows, avec la barre de titre du système : elle peut aller sur un autre écran et être
agrandie. Elle offre

* la coloration syntaxique, les numéros de ligne, les parenthèses appariées, annuler et rétablir,
  rechercher et remplacer (Ctrl+F), le retour à la ligne, l'affichage des espaces et une minimap
  (ImGuiColorTextEdit, MIT) ;
* **Charger une reconstruction** : la sortie de n'importe quel décompilateur pour ce shader, ou
  n'importe lequel de ses nettoyages IA ;
* un point d'entrée et un profil pour **tester la compilation** (F7), préremplis avec le profil du
  shader ; FXC pour le Shader Model 5, DXC pour le Shader Model 6 ;
* les erreurs et avertissements du compilateur **marqués sur leurs lignes**, avec le message en
  info-bulle, et listés sous l'éditeur : un clic amène à la ligne ;
* **Compiler et injecter** (F5) dans le jeu en cours, avec le profil propre au shader, et
  **Restaurer l'original** ;
* l'ouverture et l'enregistrement de fichiers `.hlsl` (Ctrl+O, Ctrl+S).

Le document appartient à un shader. Sélectionner un autre shader charge sa reconstruction, sauf si le
document contient des modifications : elles sont conservées, et l'onglet HLSL propose de charger le
nouveau shader à la place.

Un premier lancement ouvre sur une disposition construite plutôt que sur une pile de fenêtres dans
un coin : à gauche ce à quoi vous êtes connecté et ce parmi quoi choisir, au centre la frame, en
dessous ses commandes, à droite ce qui est sélectionné. Elle n'est construite qu'une fois, et
seulement quand le fichier ini n'en contient aucune, donc elle n'écrase jamais une disposition que
quelqu'un a faite.

## Ligne de commande

Deux options, toutes deux présentes pour qu'un lancement scripté ne dépende pas de quelqu'un qui
clique dans une liste déroulante. Tout ce qu'elles font est aussi accessible depuis l'interface.

| Option | Effet |
|---|---|
| `--connect[=<pid>]` | s'attache à une session en cours au démarrage. Sans pid elle prend la seule session présente, et refuse plutôt que de deviner quand il y en a plusieurs. |
| `--level=<nom>` | règle le niveau de suivi sur cette session : `idle`, `tracking`, `pass-timing`, `capture`, `full-draw-timing`. Appliquée après `--connect`, parce qu'un niveau ne veut rien dire tant qu'il n'y a pas de session sur laquelle le régler. |

## Threads et verrouillage

Un seul mutex par session (`SessionModel::Mutex()`). Le thread lecteur l'attrape par record, l'UI
par panneau. Règle importante : **aucune commande du pipe n'est envoyée pendant que le verrou est
tenu**, parce qu'une commande attend la réponse du thread de rendu du jeu et bloquerait le thread
lecteur pendant une frame entière.

## Disposition

La disposition construite par l'utilisateur est enregistrée dans `CyGPUInspectorApp.ini` à côté de
l'exécutable. Les viewports ImGui permettent de sortir un panneau de la fenêtre principale, pour
les configurations multi-écrans.

**Affichage > Réinitialiser la disposition** remet chaque panneau là où il est au premier
lancement. C'est aussi le moyen de retrouver un panneau sorti sur un écran qui n'est plus là, qui
reste sinon hors de vue.

## Travail de fond

Rien de coûteux ne tourne sur le thread de l'interface :

* le **thread lecteur** draine le ring et alimente le modèle ;
* le **thread d'analyse** désassemble et réfléchit les shaders, avec cache mémoire et disque ;
* le **frame graph** est reconstruit quatre fois par seconde, pas à chaque image, et la case
  `Follow` permet de geler celui d'une frame pendant qu'on l'inspecte.

## Ce qui n'y est pas encore

Décompilation HLSL (milestone 6), remplacement de shader (7), profiling GPU (8), IA (9), MCP (10),
captures offline (11).
