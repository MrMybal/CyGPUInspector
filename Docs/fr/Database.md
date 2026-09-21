# CyGPUInspectorDatabase

*Disponible aussi en [anglais](../Database.md), qui fait foi.*

**État : magasin de blobs, tags et annotations implémentés et testés** (`ShaderStore`,
`ShaderNotes`). **SQLite et profils par jeu : conçus, à venir.**

## Principe

SQLite pour les métadonnées et les requêtes, disque pour les gros blobs. Rien de coûteux n'est
recalculé deux fois : tout est mis en cache **par signature de shader**, donc reconnu d'un
lancement de jeu à l'autre, et même d'un jeu à l'autre.

```
Database/
├── cygpuinspector.db            shaders, tags, relations, captures, analyses, verdicts
└── shaders/<aa>/<signature>/
    ├── metadata.json            étage, API, shader model, tailles, première/dernière rencontre
    ├── original.dxbc | .dxil
    ├── disassembly.txt
    ├── decompiled/<backend>-<version>.hlsl   un fichier par backend, jamais écrasé
    ├── decompiled/<backend>-<version>.json   verdict de validation
    ├── notes.json               nom, tags et annotations, chacune avec son origine
    ├── analysis.md              analyses IA, datées et attribuées
    └── replacements/<nom>.hlsl + .cso

Games/<Executable>/
    ├── profile.json             shaders connus, noms donnés par l'utilisateur
    ├── tags.json
    ├── state.json               disabled / highlighted / remplacements actifs
    └── captures/
```

Le premier niveau `<aa>` est constitué des deux premiers caractères hexadécimaux de la signature :
cela évite un répertoire de plusieurs milliers d'entrées.

## Ce qui est mis en cache

Désassemblage, décompilation (par backend), nettoyage IA, classification IA, tags utilisateur,
remplacements compilés.

## Versionnement

Chaque artefact dérivé porte `{ outil, version de l'outil, date, modèle IA, version du prompt }`
(§58). C'est ce qui permet de savoir qu'un résultat est périmé et de le régénérer, plutôt que de le
faire tourner à chaque ouverture ou de garder indéfiniment une sortie d'un outil obsolète.

## Rapprochement d'un shader déjà vu

1. `signature` identique → même shader à l'octet près, tout le cache s'applique.
2. Sinon `semantic_hash` identique → même code, parties de debug différentes : le cache s'applique
   avec une mention.
3. Sinon : nouveau shader.

## Accès depuis l'interface

Toujours asynchrone : indexation en tâche de fond, chargement paresseux des blobs, listes
virtualisées. Des milliers de shaders et des dizaines de milliers de draw calls sont le cas nominal
(§61), pas le cas limite.
