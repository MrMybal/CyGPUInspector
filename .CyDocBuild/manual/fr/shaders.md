---
id: shaders
title: Trouver, inspecter et décompiler les shaders
description: Rechercher des shaders, examiner les indices et comparer les reconstructions HLSL.
section: Guide utilisateur
order: 6
sourceRevision: e4c53d62df87
---

# Trouver, inspecter et décompiler les shaders {#find-inspect-and-decompile-shaders}

## Trouver un candidat {#find-a-candidate}
Ouvrez Shaders. Triez par étage, nombre de dessins ou temps GPU disponible ; filtrez sur la frame courante si nécessaire. La recherche structurée accepte notamment stage:cs, writes:rgba16f, reads:depth et ms>0.2. Combinez les critères pour suivre un effet plutôt que de deviner à partir d’identifiants.

## Lire les indices {#read-the-evidence}
Sélectionnez un shader. Détails indique l’étage, le conteneur/modèle, la taille du bytecode, les signatures et l’usage. Code du shader propose désassemblage, liaisons déclarées, reconstructions HLSL, analyse IA et outils disponibles. Les identifiants de session sont temporaires. Les signatures reconnaissent le contenu entre sessions ; la signature sémantique ignore les métadonnées du conteneur pour les mods.

DXBC fournit généralement un assembleur SM5 lisible. DXIL utilise la représentation LLVM de DXC. La réflexion décrit les déclarations ; les liaisons d’un dessin précis nécessitent une capture profonde. Un shader dont seul l’ID est connu ne devient pas décompilable sans bytecode capturé.

## Décompiler {#decompile}
Demandez l’analyse/décompilation et attendez le résultat du worker ; l’interface reste utilisable. Les backends comprennent 3Dmigoto hlsldecompiler, cygi-dxbc et le parcours dxil-spirv/SPIRV-Cross lorsque le shader le permet. Consultez chaque reconstruction et son verdict de validation. Un backend peut refuser un shader ou produire du code non compilable.

La recompilation est un indice utile, mais ne prouve pas un rendu identique. Examinez les ressources et différences de compatibilité avant remplacement. Le HLSL reconstruit n’est jamais le code source original du jeu. Utilisez [le parcours d’édition](doc:hlsl) pour tester puis appliquer une modification intentionnelle.

DXC est nécessaire pour Shader Model 6. Consultez Outils pour le résultat de recherche du compilateur ; gardez ensemble dxcompiler.dll et le dxil.dll correspondant lorsque le paquet les fournit. Voir [la référence des décompilateurs](doc:ref-shaderdecompiler).


## Illustrations {#illustrations}

### HLSL reconstruit {#figure-0}

Une reconstruction de décompilateur dans le thème clair ; ce n’est pas le code source original du jeu.

```image
{"id":"figure-0","src":"assets/media/hlsl-fr.png","alt":"HLSL reconstruit","controls":"minimal"}
```

### Parcours de modification d’un shader {#figure-1}

Conservez le bytecode original ; relisez et compilez la reconstruction avant de l’appliquer explicitement.

```diagram
{"type":"flow","title":"Parcours de modification d’un shader","nodes":[{"id":"inspect","label":"1. Inspecter le bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruire / éditer","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compiler et vérifier","x":600,"y":0,"width":280},{"id":"apply","label":"4. Appliquer en direct","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observer ou restaurer","x":300,"y":160,"width":280},{"id":"save","label":"6. Sauver profil / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
