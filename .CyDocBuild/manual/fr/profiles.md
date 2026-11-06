---
id: profiles
title: Profils de jeu, tags et état persistant
description: Reconnaître les shaders connus et conserver noms, tags et modifications voulues.
section: Guide utilisateur
order: 10
parent: shaders
sourceRevision: 35981a9ded18
---

# Profils de jeu, tags et état persistant {#game-profiles-tags-and-persistent-state}

## Retrouver son travail {#recognise-previous-work}
Profil du jeu regroupe l’exécutable/API/adaptateur, les shaders connus et nouveaux, statistiques et décisions persistantes. Le profil appartient au jeu ; analyses et annotations des shaders sont indexées par signature dans la base.

Donnez un nom, un tag ou une note utile à un shader lorsque les indices le permettent. Une annotation conserve sa provenance (utilisateur, dérivée ou IA), sa date et, le cas échéant, modèle/version du prompt et confiance. Réutiliser une signature ne transforme pas une proposition IA en fait mesuré.

## Conserver l’état entre sessions {#preserve-changes-across-runs}
L’état sauvegardé enregistre remplacements/désactivations par signature sémantique plutôt que par ID temporaire. Examinez les contrôles d’état sauvegardé avant de réappliquer un travail. Restaurer/Activer annule la modification en direct correspondante. Un shader absent peut simplement appartenir à une scène pas encore chargée. Une signature modifiée ne doit pas recevoir aveuglément un ancien remplacement.

## Garder des données portables {#keep-data-portable}
L’application stocke du JSON lisible et des blobs de shaders dans ses dossiers de données. Sauvegardez Database et Games, ainsi que les sources/mods à conserver. Ne supprimez pas ces dossiers pour commencer à résoudre un problème de connexion ou d’aperçu. Réglages JSON et layout INI sont séparés des analyses.

Une capture hors ligne s’examine sans connecter de jeu ; elle n’exécute pas un état sauvegardé et ne rend pas le profil actif modifiable. Pour distribuer le travail, exportez explicitement un [paquet de mod](doc:mods) et vérifiez ses entrées.

Voir [la référence du stockage et des profils](doc:ref-database) pour les fichiers, signatures et provenances.


## Illustrations {#illustrations}

### Parcours de modification d’un shader {#figure-0}

Conservez le bytecode original ; relisez et compilez la reconstruction avant de l’appliquer explicitement.

```diagram
{"type":"flow","title":"Parcours de modification d’un shader","nodes":[{"id":"inspect","label":"1. Inspecter le bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruire / éditer","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compiler et vérifier","x":600,"y":0,"width":280},{"id":"apply","label":"4. Appliquer en direct","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observer ou restaurer","x":300,"y":160,"width":280},{"id":"save","label":"6. Sauver profil / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
