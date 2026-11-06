---
id: architecture
title: Composants, fichiers et circulation des données
description: Comprendre les composants et retrouver réglages, captures et travail enregistré.
section: Références
order: 17
sourceRevision: 1965fbd02211
---

# Composants, fichiers et circulation des données {#components-files-and-data-flow}

## Responsabilités {#responsibilities}
ReShade charge CyGPUInspectorRS dans le processus compatible. L’observateur transmet inventaires, commandes, captures bornées et acquittements par mémoire partagée et pipes nommés. Le standalone possède analyse, affichage, persistance, workers fournisseurs et contrôles d’autorisation. CyGPUInspectorMCP est un proxy local ; CyGPUInjector rejoue séparément les mods. Le plugin Unreal gère le chargement précoce et le viewport.

Le [schéma des échanges](doc:architecture) distingue processus du jeu et d’analyse. Les images GPU partagées empruntent un autre chemin : l’aperçu ouvre des textures compatibles plutôt que de transmettre tous les pixels dans le ring. L’export profond suit un parcours distinct de copie/lecture/écriture.

## Reconnaître les fichiers {#files-to-recognise}
| Emplacement | Rôle |
| --- | --- |
| App/Documentation | Aide hors ligne, langues, figures et références techniques |
| App/Lang | Catalogues de l’interface |
| CyGPUInspectorApp.settings.json | Préférences d’apparence, langue et fournisseur ; aucune clé API persistée |
| CyGPUInspectorApp.ini | Layout d’ancrage |
| Database | Bytecode, analyses en cache, tags et annotations |
| Games | Profil par jeu et état persistant |
| Captures | Frames sauvegardées et sorties profondes |
| Images | PNG d’aperçu sauvegardés individuellement |
| Dossiers .cygimod | Manifestes, bytecode et provenance source des mods |

Sauvegardez les dossiers entiers. Une analyse générée n’est pas une source originale du jeu. Contrôlez provenance et validation avant de réutiliser un résultat après changement de backend/modèle.

## Références développeur {#developer-references}
Les détails techniques sont regroupés sous les rubriques des tâches correspondantes. Ils expliquent les captures, le profiling, l’analyse des shaders, le stockage et les points d’intégration pris en charge, dans les deux langues.

Voir [Architecture du système](doc:ref-cygpuinspector-architecture), [IPC](doc:ref-ipc) et [API publique d’intégration](doc:api-overview).


## Illustrations {#illustrations}

### Circulation des données {#figure-0}

ReShade charge l’observateur. Le standalone possède l’analyse, les fichiers et les décisions d’autorisation.

```diagram
{"type":"flow","title":"Circulation des données","nodes":[{"id":"game","label":"Jeu + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Mémoire partagée / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Analyse standalone","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profils / mods","x":600,"y":160,"width":280},{"id":"ai","label":"Fournisseur IA / agent MCP","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```

### Deux contrôles d’autorisation indépendants {#figure-1}

Les modes de l’assistant intégré et les niveaux d’outils MCP externes sont des réglages distincts. Le standalone contrôle les deux parcours.

```diagram
{"type":"flow","title":"Deux contrôles d’autorisation indépendants","nodes":[{"id":"assistant","label":"Assistant : Auto / Demander / Tout","x":0,"y":0,"width":280},{"id":"host","label":"Contrôles dans le standalone","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP : Lire / Déboguer / Modifier","x":640,"y":0,"width":280},{"id":"provider","label":"Fournisseur CLI / API","x":0,"y":160,"width":280},{"id":"actions","label":"Actions jeu / fichiers autorisées","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```
