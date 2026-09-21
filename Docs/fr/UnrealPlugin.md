# Le plugin Unreal

*Disponible aussi en [anglais](../UnrealPlugin.md), qui est la langue de référence du projet.*

`CyGPUInspectorUnreal/CyGPUInspector` est un plugin Unreal Engine (5.3 et ultérieur) qui permet de
capturer une frame de **l'éditeur**, et d'un **build Development ou DebugGame** du jeu, avec
CyGPUInspector : depuis un bouton de la barre d'outils, une commande console ou un nœud Blueprint,
sans quitter l'éditeur, et avec les noms qu'Unreal donne à ses render targets.

## Pourquoi un plugin, et pourquoi il faut quand même ReShade

CyGPUInspector observe une frame à travers l'API d'add-ons de ReShade : c'est là que vit
CyGPUInspectorRS, et c'est une règle du projet qu'il ne développe pas sa propre interception
graphique. Or ReShade ne voit qu'un device dont il a vu la création. Dans un jeu, il est installé à
côté de l'exécutable sous le nom `dxgi.dll` et Windows le charge. L'éditeur est différent : un seul
exécutable, `UnrealEditor.exe`, partagé par tous les projets, dans le dossier du moteur.

Le plugin charge donc ReShade lui-même, **à `PostConfigInit`**, avant qu'Unreal crée son device
D3D12 — au même moment et de la même façon que le plugin RenderDoc d'Unreal charge
`renderdoc.dll`. À partir de là, tout ce qui touche au graphique est le fait de ReShade : le plugin
n'intercepte, ne patche et ne détourne rien. Il met ensuite CyGPUInspectorRS dans le dossier
d'add-ons de ReShade, qui le charge comme tout add-on qu'il y trouve. C'est important : ReShade
décharge ses add-ons quand son dernier device disparaît et les recharge avec le suivant, et Unreal
crée un device sur chaque carte avant d'en garder un. Un add-on enregistré de l'extérieur ne survit
pas à ça ; un add-on dans le dossier de ReShade, si.

**Oui, il faut un ReShade — et le bon : la version *avec support complet des add-ons*.** La version
standard désactive les add-ons dans tout programme à « forte activité réseau », pour les tenir hors
des jeux multijoueurs, et l'éditeur parle en permanence à son cache de données dérivées, à Zen, à
Live Coding, par des sockets. La version complète ne fait pas ça. C'est celle que reshade.me propose
« with full add-on support » ; son `ReShade64.dll` (ou le `dxgi.dll` qu'elle installe pour un jeu,
renommé) est ce que le plugin charge.

## Installation

1. Copier `CyGPUInspectorUnreal/CyGPUInspector` dans le dossier `Plugins/` du projet (un projet C++
   le compile ; pour un projet Blueprint seulement, utiliser une version construite avec
   `RunUAT BuildPlugin`).
2. Le zip de publication du plugin contient déjà ce qu'il faut dans
   `Plugins/CyGPUInspector/Binaries/ThirdParty/CyGPUInspector/Win64/` : `ReShade64.dll` (la version
   officielle de ReShade 6.8.0 avec support complet des add-ons, avec ses notices de licence) et
   `CyGPUInspectorRS.addon64`. Depuis le dépôt source, qui ne contient jamais ReShade, lancer
   `python Tools/fetch_reshade.py --with-addon` : le script télécharge la version figée de ReShade
   sur reshade.me, la vérifie par le SHA-256 qu'il connaît, extrait `ReShade64.dll` de
   l'installateur sans l'exécuter, et la place là avec l'add-on. Chaque fichier peut aussi être
   ailleurs : indiquer son chemin dans **Project Settings > Plugins > CyGPUInspector**.
3. Indiquer le chemin de `CyGPUInspectorApp.exe` dans les mêmes réglages, sauf si l'add-on vient
   directement d'un paquet CyGPUInspector : le standalone est alors trouvé dans le dossier `App` à
   côté d'`Addons`.
4. Redémarrer l'éditeur. L'Output Log dit ce qui a été chargé, d'où, ou pourquoi rien ne l'a été
   (`LogCyGPUInspectorLoader`).

ReShade garde son `ReShade.ini` et son `ReShade.log` dans `Saved/CyGPUInspector/ReShade/` du projet
(par `RESHADE_BASE_PATH_OVERRIDE`) plutôt que dans le dossier `Binaries` du moteur, que tous les
projets partagent. Si ReShade est déjà installé à côté de l'exécutable (un `dxgi.dll` qui est
ReShade), le plugin utilise celui-là au lieu d'en charger un second.

## Capturer

| Depuis | Comment |
|---|---|
| la barre d'outils de l'éditeur de niveau | **Capture** — nombre de frames et options des réglages |
| le menu **CyGPUInspector** à côté | capturer 1, 2 ou 4 frames ; cocher buffers, descripteurs, barrières, timestamps ; ouvrir le standalone ; réglages ; état |
| Tools | *Capture a Frame with CyGPUInspector*, *Open CyGPUInspector* |
| la console | `CyGPUInspector.Capture [frames]`, `CyGPUInspector.Status`, `CyGPUInspector.OpenStandalone` |
| les Blueprints | `Capture Frames`, `Is CyGPUInspector Available`, `Get CyGPUInspector Status`, `Open CyGPUInspector` |

Une capture a besoin du standalone : c'est lui qui reçoit les frames et les enregistre. Quand aucun
n'est connecté, le plugin le lance avec `--connect=<PID de l'éditeur>`, attend qu'il se connecte
(30 secondes au plus), puis demande la capture à l'add-on — dans le processus, par les trois
fonctions d'[`InProcessApi.h`](../../CyGPUInspectorCore/Include/CyGPUInspectorCore/InProcessApi.h),
pas par la fenêtre du jeu. Une notification dit quand la capture est armée, quand elle est finie et
quand elle a échoué. Les fichiers arrivent là où arrivent ceux de toute capture profonde :
`Images/<processus>_<date>_<heure>_frame<N>/` à côté du standalone, voir
[CaptureModes.md](CaptureModes.md#ce-quelle-enregistre-sur-le-disque).

**Ce qui est capturé, c'est le rendu 3D du viewport principal, et rien d'autre.** Le viewport
principal est le Play In Editor quand il tourne, sinon le viewport de niveau dans lequel on a
travaillé en dernier (dans un jeu, le viewport du jeu). L'interface de l'éditeur — panneaux, menus,
info-bulles, autres fenêtres — est laissée de côté.

Comment : par une *scene view extension*, le point d'accroche du moteur dans le rendu d'un
viewport, le plugin efface une texture 1x1 à lui juste avant le rendu 3D de ce viewport, et une
autre juste après. Ces deux effacements sont des commandes de rendu ordinaires : l'add-on les voit
passer comme les autres, par ReShade, et ne garde que ce qui se trouve entre les deux — les
commandes, leurs bindings, les buffers qu'elles ont écrits. L'extension dit aussi à l'add-on dans
quelle texture ce rendu se termine (la render target du viewport dans l'éditeur), et cette texture
devient l'image finale, affichée en direct dans le standalone et enregistrée en `final.png`, au lieu
de toute la fenêtre de l'éditeur. Les marqueurs coûtent deux effacements d'un pixel par frame de ce
viewport, et ne sont ajoutés que si l'add-on est dans le processus.

Une capture attend une frame qui contient ce rendu : un viewport de niveau qui ne se redessine que
quand quelque chose change est prié de se redessiner, et une capture abandonne au bout d'environ dix
secondes sans lui (« le viewport est-il visible, et dessine-t-il ? »). La timeline du standalone
montre toujours toutes les commandes de chaque frame ; le panneau Capture profonde dit à quelles
commandes la capture a été limitée. Dans un jeu packagé, le viewport dessine directement dans la
fenêtre, donc son image finale contient l'interface du jeu (UMG) dessinée par-dessus la scène.

**Les frames sont comptées sur la fenêtre principale.** L'éditeur présente une swap chain par
fenêtre. L'add-on ne termine une frame que sur la plus grande ; les autres passent sans qu'il y
touche. La première version comptait chaque present comme une frame : l'image finale partagée était
recréée à chaque present avec une nouvelle taille — et détruite alors que le GPU pouvait encore
copier dedans, ce qui a bloqué le GPU et figé la machine. Les textures partagées remplacées sont
maintenant mises de côté et détruites plusieurs frames plus tard, et en Direct3D 12 plus rien n'est
jamais copié depuis une texture dont aucune transition n'a révélé l'état.

## Les builds de debug du jeu

Le loader et tout ce qui parle à ReShade sont construits pour **Development et DebugGame**, jamais
pour **Shipping** : le module loader est exclu de Shipping dans le `.uplugin`, et le reste y est
compilé à vide. Un projet peut donc garder les nœuds Blueprint dans son code : en Shipping, et sur
les plateformes autres que Windows, ils ne font rien et le disent.

Un build Development ou DebugGame packagé emporte `ReShade64.dll` et l'add-on quand ils sont dans
le dossier `ThirdParty` du plugin (ils sont déclarés comme dépendances d'exécution), et les charge
au démarrage de la même façon. **Project Settings > Plugins > CyGPUInspector > Load In Game** le
désactive ; `-NoCyGPUInspector` le désactive pour un lancement, `-CyGPUInspector` le force.

## Les noms

Dans l'éditeur, en Development et en DebugGame, Unreal nomme chaque ressource GPU qu'il crée par
`ID3D12Object::SetName` : `SceneDepthZ`, `GBufferA`, `SceneColorDeferred`, `HZBFurthest`… Pendant
une capture profonde, l'add-on lit ces noms sur les objets natifs que ReShade lui donne — il les
lit, il n'intercepte rien — et les envoie au standalone. Ils apparaissent dans le panneau
Ressources, dans le nom de chaque buffer enregistré
(`03_rt_1920x1080_r16g16b16a16_float_SceneColorDeferred.png`) et dans `capture.json`. Cela marche
pour tout programme Direct3D 11 ou 12 qui nomme ses ressources, pas seulement Unreal ; les builds
Shipping ne le font généralement pas.

Ce que le plugin ne peut pas encore donner, ce sont les noms des **passes**. Unreal les émet comme
marqueurs de debug (`BeginEvent`) quand on le lui demande, mais ReShade n'a pas d'événement d'add-on
pour un marqueur émis par l'application, donc l'add-on ne les voit pas. Le standalone continue de
déduire les noms des passes de ce que fait chacune.

## Ligne de commande

| Option | Effet |
|---|---|
| `-CyGPUInspector` | charger même si les réglages disent non |
| `-NoCyGPUInspector` | ne pas charger, quels que soient les réglages |
| `-CyGPUInspectorReShade=<chemin>` | le ReShade à charger |
| `-CyGPUInspectorAddon=<chemin>` | l'add-on à charger |

Rien n'est chargé dans un commandlet (cook, `-run=`), avec `-nullrhi` ou `-server`.

## Où ça vit

| Partie | Fichier |
|---|---|
| Chargement de ReShade et de l'add-on | `CyGPUInspectorUnreal/CyGPUInspector/Source/CyGPUInspectorLoader/` |
| Réglages, commandes console, nœuds Blueprint, déroulé d'une capture | `.../Source/CyGPUInspector/` |
| Barre d'outils, menus, notifications | `.../Source/CyGPUInspectorEditor/` |
| L'interface dans le processus de l'add-on | `CyGPUInspectorCore/Include/CyGPUInspectorCore/InProcessApi.h`, `CyGPUInspectorRS/Source/Addon/InProcess.{hpp,cpp}` |
| Noms des ressources | `CyGPUInspectorRS/Source/Tracking/ResourceNames.{hpp,cpp}` |

Le plugin a sa propre copie d'`InProcessApi.h`
(`Source/ThirdParty/CyGPUInspector/CyGPUInspectorInProcessApi.h`), parce qu'Unreal le compile avec
sa propre chaîne d'outils ; CMake réécrit cette copie dès que l'original change.
