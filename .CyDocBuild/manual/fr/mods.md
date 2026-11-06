---
id: mods
title: Exporter et utiliser un paquet de mod
description: Exporter les modifications choisies et appliquer le paquet avec CyGPUInjector.
section: Guide utilisateur
order: 11
parent: hlsl
sourceRevision: 42877cf8275f
---

# Exporter et utiliser un paquet de mod {#export-and-use-a-mod-package}

## Exporter une modification voulue {#export-an-intentional-change}
1. Réalisez et vérifiez un remplacement ou une désactivation en direct.
2. Ouvrez Export de mod et relisez les actions enregistrées. La dernière décision pour un shader remplace les précédentes.
3. Cochez uniquement les entrées à distribuer. Les surbrillances magenta sont décochées car elles servent au diagnostic.
4. Complétez les métadonnées puis exportez tout le dossier .cygimod.
5. Examinez mod.json et les fichiers de shaders/sources avant partage.

Restaurer ou Activer retire la modification enregistrée correspondante. L’export décrit des décisions replace/disable ; il ne copie pas tous les shaders, un état runtime arbitraire ni un replay GPU.

## Appliquer sans l’inspecteur {#apply-without-the-inspector}
CyGPUInjector.addon64 est le petit add-on ReShade qui applique les paquets exportés sans standalone ni IPC. Installez-le par le parcours normal de ReShade avec add-ons. Suivez les emplacements de la [référence des mods](doc:ref-modpackages) ; contrôlez son overlay et ReShade.log pour les messages de chargement/conflit.

La correspondance utilise le hash sémantique du shader : un nouvel ID de session ne la casse pas. Un vrai changement de code doit empêcher une entrée périmée de correspondre. Un remplacement prend effet à la création du pipeline, généralement au chargement ; activer un paquet après cela peut demander un redémarrage/recréation. Les désactivations s’appliquent aux dessins/dispatches correspondants. Si deux paquets ciblent le même shader, l’ordre alphabétique décide et un avertissement est journalisé.

## Vérifier et revenir en arrière {#verify-and-roll-back}
Testez le paquet exporté indépendamment sur une scène répétable. Gardez source et métadonnées pour le suivi. Retirez/désactivez le paquet puis redémarrez/recréez les pipelines selon le besoin pour revenir à un lancement normal. Malgré son nom, l’injecteur reste un add-on chargé par ReShade ; il ne contourne pas les restrictions d’installation.


## Illustrations {#illustrations}

### Parcours de modification d’un shader {#figure-0}

Conservez le bytecode original ; relisez et compilez la reconstruction avant de l’appliquer explicitement.

```diagram
{"type":"flow","title":"Parcours de modification d’un shader","nodes":[{"id":"inspect","label":"1. Inspecter le bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruire / éditer","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compiler et vérifier","x":600,"y":0,"width":280},{"id":"apply","label":"4. Appliquer en direct","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observer ou restaurer","x":300,"y":160,"width":280},{"id":"save","label":"6. Sauver profil / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
