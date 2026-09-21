# Packages de mods — exporter une modification, la rejouer sans l'outil

*Disponible aussi en [anglais](../ModPackages.md), qui fait foi.*

**État : implémenté et testé** (`Tests/CyGPUInspectorShaderTests`, 19 vérifications sur l'aller-retour
complet avec de vrais shaders compilés, y compris le refus d'un byte code corrompu).

Tout ce que CyGPUInspector fait à un jeu qui tourne meurt avec le jeu. L'add-on garde les
remplacements en mémoire, et les identifiants qu'ils utilisent ne veulent plus rien dire au
lancement suivant. Un **package de mod** est ce travail écrit noir sur blanc, et
**CyGPUInjector** est le petit add-on qui le rejoue — sans inspecteur, sans standalone, sans IPC.

```
CyGPUInspector            →   MyMod.cygimod   →   CyGPUInjector.addon64
(on trouve et on modifie)     (un dossier)        (on applique, chez soi ou chez les autres)
```

## 1. La clé : le hash sémantique

Une entrée de package ne désigne pas « le shader n° 47 ». Elle désigne le **SHA-256 des chunks de
code** d'un shader, à l'exclusion des parties debug et réflexion du conteneur. C'est ce qui fait
qu'un package :

* survit au redémarrage du jeu, où toute numérotation a changé ;
* survit à un recompile qui ne change que les métadonnées du conteneur ;
* **cesse de s'appliquer** quand le jeu change vraiment ce shader.

Ce dernier point est une fonctionnalité, pas un défaut. Un package périmé ne trouve plus rien et
ne fait rien, plutôt que d'appliquer votre modification à un shader qui n'est plus le bon.

## 2. Ce qu'un package contient

Un **dossier**, pas un blob opaque — même raison qu'une capture : on doit pouvoir regarder dedans,
comparer deux versions, corriger une note à la main.

```
MyMod.cygimod/
  mod.json                  manifeste : métadonnées et entrées
  shaders/<hash>.cso        byte code de remplacement, nommé par le hash qu'il remplace
  shaders/<hash>.hlsl       le HLSL dont il a été compilé — provenance, pas exécution
```

Deux actions, et seulement deux :

| Action | Effet |
|---|---|
| `replace` | le byte code du package est substitué quand le jeu crée le pipeline |
| `disable` | chaque draw ou dispatch utilisant ce shader est supprimé |

Le HLSL source est conservé pour qu'on puisse, dans un an, voir **ce qui était voulu** et pas
seulement ce qui a été compilé. Il n'est jamais exécuté.

## 3. Exporter

Panneau **Mod export** du standalone. Tout ce qu'on fait au jeu y arrive tout seul :

* un shader remplacé depuis l'éditeur HLSL → entrée `replace`, cochée ;
* un shader désactivé depuis Details → entrée `disable`, cochée ;
* un **highlight** magenta → entrée `replace`, **décochée**. C'est une façon de regarder un shader,
  pas quelque chose que quelqu'un veut recevoir dans un mod ; elle est enregistrée pour ne rien
  perdre, mais elle ne peut pas partir par inattention.
* `Restore` ou `Enable` sur un shader → l'entrée est **oubliée**. Exporter une modification que
  l'utilisateur a reprise reviendrait à exporter une erreur.

Un shader modifié deux fois ne garde que la dernière décision : c'est ce que l'utilisateur voit
dans le jeu, donc c'est ce qu'un export doit vouloir dire.

## 4. Appliquer — CyGPUInjector

Copier le dossier `*.cygimod` dans `CyGPUInjector/` à côté de l'exécutable du jeu, et déposer
`CyGPUInjector.addon64` là où ReShade charge ses add-ons. Un autre chemin se règle avec `ModPath`
dans la section `[CYGPUINJECTOR]` de `ReShade.ini`.

L'onglet **CyGPUInjector** de l'overlay ReShade montre ce qui est chargé, combien de shaders ont
réellement été remplacés, combien de draws ont été supprimés, et permet de désactiver un package.

### Comment il applique, exactement

* **Remplacement** — `create_pipeline` donne à un add-on la description que le jeu s'apprête à
  utiliser, et le laisse la modifier. Le byte code du package y est écrit avant que l'API graphique
  ne voie jamais l'original. C'est le chemin simple, et il est disponible ici précisément parce que
  les modifications sont **connues d'avance** ; l'inspecteur, lui, doit faire beaucoup plus
  compliqué, puisqu'il ne découvre le shader qu'il veut remplacer que bien après la création du
  pipeline (voir [ShaderReplacement.md](ShaderReplacement.md)).
* **Suppression** — un rappel de draw renvoie `true`, ce qui fait que ReShade laisse tomber la
  commande.

### Ce que ça coûte

Presque rien, et c'est mesurable dans le choix des événements enregistrés : un package qui ne fait
que remplacer des shaders **n'enregistre aucun rappel par draw**. Le seul travail est un hash par
shader au moment où le jeu crée ses pipelines, ce qui arrive quelques milliers de fois sur une
partie entière, pas par frame. Les rappels par draw ne sont branchés que si un package désactive
réellement quelque chose.

## 5. Limites, dites franchement

* **Un remplacement prend effet quand le jeu crée le pipeline**, c'est-à-dire en général au
  chargement. Activer un package depuis l'overlay en cours de partie ne change rien tant que le
  pipeline concerné n'est pas recréé ; l'overlay le dit. Les suppressions de draw, elles, prennent
  effet immédiatement. L'échange à chaud existe dans l'inspecteur, pas ici : le but de
  CyGPUInjector est d'être petit.
* **Deux packages qui touchent le même shader** : le premier dans l'ordre alphabétique gagne, et
  l'autre est ignoré avec un avertissement dans `ReShade.log`. Laisser le second écraser le premier
  silencieusement rendrait le résultat dépendant de quelque chose que personne ne voit.
* **Le byte code est vérifié au chargement**, pas au moment de l'appliquer : un `.cso` tronqué ou
  édité à la main fait échouer le package entier plutôt que d'atteindre un pilote graphique.
* Le nom du jeu inscrit dans le manifeste est **indicatif**. L'appariement se fait par hash, donc
  un package fonctionne dans n'importe quel jeu qui utilise le même shader. L'overlay affiche le
  jeu d'origine, ce qui explique un package qui se charge sans jamais rien trouver.

## 6. Ce que CyGPUInjector n'est pas

Le nom dit « injector », mais **rien ici n'injecte quoi que ce soit**. C'est un add-on ReShade
comme un autre : ReShade le charge, et les modifications passent par l'API add-on officielle.
CyGPUInspector n'implémente aucun système d'injection graphique et n'en implémentera pas (§2 du
brief), et ne contourne aucune protection d'aucune sorte (§3). Un jeu qui refuse ReShade est
simplement un jeu hors de portée.

## 7. Où ça vit dans le code

| Morceau | Fichier |
|---|---|
| Le format, lu et écrit par le même code des deux côtés | `CyGPUInspectorCore/{Include/CyGPUInspectorCore,Source}/ModPackage.*` |
| Ce que l'utilisateur a changé, dans la session | `CyGPUInspectorApp/Source/Mod/ModRecorder.*` |
| Le panneau d'export | `CyGPUInspectorApp/Source/App/ModExportPanel.cpp` |
| Chargement des packages, résolution en une table | `CyGPUInjector/Source/ModLibrary.*` |
| Application via l'API add-on | `CyGPUInjector/Source/Apply.cpp` |
| Onglet d'overlay | `CyGPUInjector/Source/Overlay.cpp` |
