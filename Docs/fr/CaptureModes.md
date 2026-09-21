# Les deux captures

*Disponible aussi en [anglais](../CaptureModes.md), qui fait foi.*

**État : implémentées et testées** (`Tests/CyGPUInspectorIpcTests`, 141 vérifications, dont le
chemin complet d'une capture profonde : armement, enregistrement par commande, désarmement
automatique et aller-retour sur disque).

CyGPUInspector a **deux** captures. Ce ne sont pas deux réglages d'un curseur, ce sont deux outils.
Demander « plus de détail » à la capture runtime finirait par faire tomber le jeu à genoux ;
demander à la capture profonde de tourner en continu le ferait immédiatement.

| | **Runtime** | **Profonde** |
|---|---|---|
| Durée | continue, on la laisse tourner en jouant | quelques frames, puis elle s'arrête seule |
| Coût dans le jeu | quelques pourcents | important, assumé |
| Ce qu'elle donne | le flux de commandes, les frontières de passes, le temps GPU par passe, l'historique des frames | tout ça, plus chaque descripteur de chaque commande, l'état fixe de chaque pipeline, les barrières, et un timestamp **par commande** |
| Panneau | `Frame timeline` | `Deep capture` |
| Outils MCP | `get_frame_timeline` | `start_deep_capture`, `get_capture_state`, `get_command_state` |

---

## 1. La capture runtime — `Frame timeline`

C'est le panneau à laisser ouvert. Il est organisé comme la vue de timing d'un profileur :

* **la bande de frames** — une barre par frame sur les dernières centaines, la plus récente à
  droite, sa hauteur le temps CPU de la frame et sa couleur le budget dans lequel elle tient
  (60 fps, 30 fps, aucun). Un trait bleu marque le temps GPU de la même frame : on lit
  directement sur l'écart si la frame était limitée par le CPU ou par le GPU. La frame dont les
  timings sont affichés en dessous est encadrée. Un pic reste visible plusieurs secondes au lieu
  d'être un chiffre qui a déjà changé.
* **la vue de timing** — une frame sur un axe de temps, avec une règle en millisecondes et trois
  rangées : la frame entière, ses passes, et les commandes mesurées à l'intérieur. Chaque barre est
  placée **là où elle s'est exécutée sur le GPU**, pas bout à bout : l'add-on envoie l'instant de
  début de chaque timestamp, pas seulement sa durée. Molette pour zoomer autour de la souris,
  glisser pour se déplacer, double-clic ou `Ajuster` pour revoir la frame entière. Cliquer une
  barre sélectionne la passe ou la commande partout ailleurs dans l'outil.
* **la liste des passes et la passe sélectionnée**, sous la vue.

**Couleur = nature de la passe** (graphique, compute, transfert), **longueur = temps**. Une passe
sans timestamp propre est placée entre les mesures qui l'entourent et dessinée **en
transparence** ; sa durée est affichée avec un `~` dans la liste, parce que c'est une estimation.
La rangée des commandes n'apparaît que lorsqu'elle en dit plus que celle des passes — en
`Pass Timing` il y a une mesure par passe, et les deux rangées seraient les mêmes barres deux fois.

La vue, la liste, la passe sélectionnée et l'outil MCP `get_frame_timeline` lisent tous une seule
disposition (`Analysis/FrameTimeline`) : une passe a une seule durée, où qu'elle soit affichée.

### Deux vues : une frame, ou continue

Le panneau offre deux vues de la même capture, choisies en haut (`Une frame` / `Continue`), chacune
avec sa propre disposition :

* **Une frame** est ce qui est décrit plus haut : la frame la plus récente, décortiquée.
* **Continue** reprend la disposition de la vue de timing d'un profileur. Sous la bande de frames,
  chaque frame mesurée des dernières secondes est placée à la suite de la précédente sur
  l'**horloge du GPU** : une rangée de frames — chaque barre est le temps pendant lequel le GPU
  était occupé par cette frame, et entre deux frames une barre hachurée montre le temps pendant
  lequel il a **attendu** — puis les passes de chaque frame en dessous, puis ses commandes mesurées.
  Cliquer une frame dans la bande l'amène dans la vue et l'encadre ; les frames affichées sont
  surlignées dans la bande. Sous la vue, la frame choisie est listée passe par passe, avec l'instant
  où chaque passe commence dans la frame et sa durée : un pic se lit après coup, pas seulement
  pendant qu'il est à l'écran. Molette, glisser et double-clic fonctionnent comme dans l'autre
  vue ; le double-clic ou `Ajuster` reviennent au suivi de la frame la plus récente.

Ce qui rend la vue continue, c'est que l'add-on envoie, pour chaque frame mesurée, où elle commence
et se termine sur l'horloge du GPU (`FrameGpuSpanRecord`). L'attente entre deux frames est l'écart
entre le timestamp de fermeture de l'une et la première commande mesurée de la suivante : un temps
pendant lequel le GPU a soit attendu du travail, soit exécuté des commandes situées avant le
premier timestamp. L'infobulle dit les deux, parce que la capture ne peut pas les distinguer.
Quand des frames manquent — jetées parce que l'interface a pris du retard, ou jamais mesurées —
l'écart indique combien au lieu de l'appeler une attente.

Chaque frame est découpée en passes avec **ses propres** commandes. Les timings arrivent trois ou
quatre frames après la frame qu'ils mesurent, donc l'application garde les huit dernières frames
entières (`SessionModel::FrameByIndex`) ; une frame dont les commandes n'étaient plus là est
découpée avec celles d'une frame plus récente, et le dit. Ce qui est gardé par frame est compact —
noms, positions, durées — si bien que les 600 dernières frames restent consultables longtemps après
la disparition de leurs commandes (`Analysis/FrameTrack`).

Un add-on antérieur à `FrameGpuSpanRecord` fonctionne toujours : les frames sont alors espacées
d'une frame CPU, et la vue indique que les attentes ne sont pas mesurées.

Il faut un niveau de suivi `Pass Timing` ou plus pour que les barres aient une longueur. Sans
timings la même vue est disposée selon le nombre de commandes, la règle affiche `commandes` au lieu
de `ms`, et le panneau dit pourquoi.

### D'où viennent les noms

De nulle part, au sens strict : **aucune API graphique n'expose ses marqueurs de debug à un add-on
ReShade** (pas de `PIXBeginEvent`, pas d'`ID3DUserDefinedAnnotation` — voir
[Research/ReShadeAddonAPI.md](Research/ReShadeAddonAPI.md)). Les noms sont **dérivés** de ce que
chaque commande lit et écrit, et chaque nom est affiché avec son origine et sa confiance. Un nom
donné par l'utilisateur est marqué comme tel. Une déduction n'a jamais le droit de ressembler à un
fait.

C'est la différence de fond avec Nsight ou PIX, qui lisent les marqueurs que le moteur a posés.
Là où ils affichent `ShadowMapPass` parce que le jeu l'a écrit, CyGPUInspector affiche
`Shadow map (heuristique, 72 %)` parce qu'il l'a déduit.

---

## 2. La capture profonde — `Deep capture`

Un coup unique. On l'arme (bouton, menu `File`, ou `start_deep_capture` en MCP), l'add-on bascule
sur le chemin coûteux pendant *n* frames, puis **se désarme tout seul** — y compris si quelque
chose se passe mal. Une capture qui oublie de se désarmer laisserait le jeu sur le chemin coûteux
pour toujours ; c'est le seul comportement qui n'était pas négociable dans cette partie.

### Ce qu'elle enregistre, par commande

* **chaque descripteur lié** : buffers de constantes (avec offset et taille), textures, UAV,
  samplers, par registre et par étage — `b0`, `t3`, `u1`, `s0`, comme on les lit en HLSL ;
* les **vertex buffers** et l'**index buffer**, avec leur format ;
* le **viewport** et le **scissor** ;
* l'ensemble des **render targets** et le depth stencil ;
* la **topologie** primitive ;
* le **temps GPU de cette commande précise**, pas de sa passe.

### Ce qu'elle enregistre, par pipeline

L'état fixe lu à la création : test et écriture de profondeur, fonction de comparaison, stencil,
mode de culling, sens d'enroulement, blending et masque d'écriture, formats des cibles, nombre
d'échantillons. Chaque champ est accompagné d'un drapeau disant si l'API l'a **réellement**
rapporté, pour que l'interface puisse distinguer « test de profondeur désactivé » de « l'API n'a
rien dit ».

### Et les barrières

Chaque transition de ressource devient un événement sur la timeline, entre les commandes qu'elle
sépare. C'est intéressant précisément parce que le graphe de frame, lui, *déduit* les dépendances
des lectures et des écritures : quand les deux ne sont pas d'accord, il y a quelque chose à voir.

### Ce qu'elle enregistre sur le disque

Chaque capture profonde a son propre dossier à côté de l'exécutable du standalone, écrit sans un
clic :

```
Images/<jeu>_<date>_<heure>_frame<N>/
    final.png                       ce que le jeu a présenté pour la frame capturée
    01_depth_2560x1440_d32_float_res812.png / .dds
    02_rt_2560x1440_r16g16b16a16_float_res815.png / .dds
    ...
    capture.json                    chaque buffer, ce qu'il était pour la frame, et pourquoi certains n'ont pas été enregistrés
```

**Enregistrer les buffers** (coché par défaut) demande à l'add-on de copier, à la fin de la
dernière frame capturée, chaque texture dans laquelle cette frame a écrit : cibles de rendu (les
huit emplacements quand les descripteurs sont enregistrés, les quatre premiers sinon), buffers de
profondeur, textures UAV écrites par des compute shaders, destinations de copie et de resolve.
Chacune est copiée sur la propre file du jeu dans une texture partagée à elle — le même mécanisme
que l'aperçu, voir [GPUSharing.md](GPUSharing.md) — et le standalone relit chacune une fois et
écrit deux fichiers :

* un **DDS** avec les données elles-mêmes, dans le format du jeu (un buffer de profondeur via sa
  famille typeless : `d32_float` est enregistré en `r32_float`), pour les outils qui ont besoin des
  vraies valeurs — couleur HDR, profondeur, normales, vecteurs de mouvement ;
* un **PNG** à regarder : la couleur telle quelle, élargie au-delà de 0–1 quand la texture contient
  vraiment des valeurs au-delà (HDR, vecteurs signés, le demi-pour-cent extrême ignoré) ; la
  profondeur étirée entre ses valeurs la plus proche et la plus lointaine, sans les valeurs
  d'effacement, sinon un buffer de profondeur brut n'est qu'un gris uniforme. La plage utilisée est
  écrite dans `capture.json`.

Les fichiers sont nommés dans l'ordre où la frame a utilisé les textures pour la première fois,
qui est l'ordre dans lequel on lit une frame. L'onglet **Images** du panneau Capture profonde,
qui s'ouvre de lui-même à la fin d'une capture, les liste à côté de l'image capturée ; un clic en
affiche un, relu depuis son PNG, donc ce qui est à l'écran est exactement ce qui a été écrit.
**Ouvrir le dossier** ouvre le dossier. Les commandes enregistrées sont dans l'onglet
**Commandes** juste à côté.

Trois choses à savoir :

* **chaque buffer est tel qu'il était à la *fin* de la frame.** Une texture dans laquelle plusieurs
  passes dessinent tour à tour montre la dernière, et une texture réutilisée pour autre chose plus
  tard dans la frame montre cet autre chose. Voir l'état entre deux passes demanderait une copie au
  milieu des command lists du jeu ; ce n'est pas fait ;
* **en Direct3D 12, une texture n'est copiée que depuis un état connu.** La copier, c'est la sortir
  de l'état où elle se trouve, et en D3D12 cet état n'est connu que par les barrières que le jeu a
  enregistrées, d'où l'activation de l'enregistrement des barrières quand on enregistre les
  buffers. Une texture pour laquelle aucune barrière n'a été vue est listée *non copiée* plutôt que
  devinée : une mauvaise supposition est un comportement indéfini, et sur un buffer de profondeur
  compressé, ça se voit ;
* **les copies coûtent de la mémoire GPU au jeu jusqu'à leur enregistrement** : 64 buffers et
  1,5 Gio au plus. Le standalone les relit une par frame d'interface, quelques frames après la
  capture pour que le GPU les ait forcément faites, puis dit à l'add-on de les libérer. Si le
  standalone ne le demande jamais, elles sont libérées au bout d'une minute. Seul le mip 0 de la
  couche 0 est copié, et les textures multi-échantillonnées sont listées mais pas copiées.

### Garde-fous

* un **plafond par frame** (60 000 commandes par défaut) : une frame pathologique ne peut pas faire
  exploser la mémoire du jeu ;
* un **plafond par commande** (256 descripteurs) ; au-delà, le compte des descripteurs perdus est
  publié plutôt que passé sous silence ;
* la frame capturée est **conservée à part** de la frame courante. Sans ça, la frame qu'on vient de
  demander au jeu serait remplacée un soixantième de seconde plus tard.

---

## 3. Ce que ni l'une ni l'autre ne fait : rejouer

Il faut le dire franchement, parce que c'est la question que pose quiconque connaît RenderDoc.

Un débogueur de frame comme **RenderDoc sérialise le flux de commandes et le ré-exécute sur son
propre device**. C'est ce qui lui achète l'historique de pixel, le pas-à-pas sur un draw, la
modification d'un paramètre suivie d'un rendu immédiat, le *mesh viewer* après le vertex shader.
Tout cela découle du rejeu, pas de l'observation.

CyGPUInspector **observe la vraie frame à travers l'API add-on officielle de ReShade et n'injecte
jamais son propre device**. C'est une décision de la spécification (§2), pas un trou à boucher :
le projet ne réimplémente ni hooking D3D, ni proxy de DLL, ni interception de swapchain. La
conséquence est mécanique : il rapporte ce qui s'est passé, il ne le rejoue pas.

Concrètement, sont **hors de portée** et le resteront tant que cette contrainte tient :

* l'historique de pixel (« quels draws ont touché ce pixel, et avec quelle valeur ») ;
* le pas-à-pas avec ré-exécution d'une commande isolée ;
* la visualisation de la géométrie après le vertex shader ;
* la modification d'un état suivie d'un re-rendu de la même frame.

Ce qui **reste possible** et est livré : voir l'état complet de chaque commande, remplacer un
shader à chaud et observer la frame suivante, désactiver un shader pour voir ce qui disparaît,
prévisualiser n'importe quelle ressource par texture partagée sans readback CPU.

Une limite de plus, celle-là purement technique : une **table de descripteurs** Direct3D 12 (ou un
descriptor set Vulkan) est liée par *handle*, et l'API add-on ne donne pas le contenu du tas. Ces
descripteurs-là ne sont pas lisibles. La commande est alors marquée comme **incomplète** dans
l'interface, plutôt que d'afficher une liste vide qui se lirait comme « rien n'était lié ».

---

## 4. Ce que ça coûte au jeu

La capture runtime est faite pour être laissée en marche. La capture profonde ne l'est pas, et la
manière dont c'est garanti mérite d'être expliquée, parce que c'est le point délicat de tout
l'add-on.

Les rappels qui suivent les liaisons (`push_descriptors`, `bind_vertex_buffers`, `bind_viewports`,
`barrier`…) sont **enregistrés une fois pour toutes** auprès de ReShade : l'API ne permet pas à un
add-on d'ajouter et de retirer des gestionnaires en cours de route sans courir après les threads du
jeu. Ce qui les rend gratuits hors capture, c'est leur première ligne : une lecture atomique
relâchée qui dit qu'aucune capture ne tourne, et un retour immédiat. Le prix en mode runtime est
une branche prévisible par appel de liaison, et rien d'autre.

Le coût de l'outil lui-même est mesuré et publié à chaque frame (`addon_cpu_ms`), et affiché dans
le panneau : on n'a pas à le croire sur parole.

---

## 5. Où ça vit dans le code

| Morceau | Fichier |
|---|---|
| Vocabulaire du protocole (`CaptureMode`, `DrawStateRecord`, `BarrierEntry`, `PipelineStateRecord`) | `CyGPUInspectorCore/Include/CyGPUInspectorCore/Protocol.hpp` |
| Machine à états de la capture profonde, armement et désarmement | `CyGPUInspectorRS/Source/Tracking/DeepCapture.{hpp,cpp}` |
| Suivi de l'état Direct3D pendant une capture | `CyGPUInspectorRS/Source/Tracking/StateShadow.{hpp,cpp}` |
| Rappels ReShade et publication | `CyGPUInspectorRS/Source/Addon/DeviceContext.cpp` |
| Modèle côté standalone, historique des frames, frame capturée | `CyGPUInspectorApp/Source/Session/SessionModel.{hpp,cpp}` |
| Les deux panneaux | `CyGPUInspectorApp/Source/App/CapturePanels.cpp` |
| Persistance (`drawstates.bin`, `barriers.bin`) | `CyGPUInspectorApp/Source/Session/CaptureArchive.cpp` |
