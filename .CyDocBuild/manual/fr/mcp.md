---
id: mcp
title: Connecter un agent externe avec MCP
description: Connecter un agent externe et choisir les accès autorisés par le standalone.
section: Guide utilisateur
order: 13
parent: ai
sourceRevision: dbc7bdaa2f41
---

# Connecter un agent externe avec MCP {#connect-an-external-agent-with-mcp}

## Configurer le client {#configure-the-client}
Lancez d’abord le standalone. Configurez le client MCP pour exécuter App/CyGPUInspectorMCP.exe en transport stdio avec son vrai chemin d’installation. Le proxy transmet le JSON-RPC ligne par ligne au pipe local de l’application ; celle-ci possède session, données et autorisations. Le proxy ne capture pas un jeu et ne s’accorde pas de droits.

```json
{"mcpServers":{"CyGPUInspector":{"command":"C:/Tools/CyGPUInspector/App/CyGPUInspectorMCP.exe"}}}
```

Le chemin est un exemple à remplacer. La syntaxe exacte dépend du client. Connectez un producteur ou ouvrez une sauvegarde, puis demandez à l’agent d’inspecter session/frame avant une analyse coûteuse.

## Choisir le niveau d’outils externe {#select-the-external-tool-level}
| Niveau | Capacités |
| --- | --- |
| Lecture seule (défaut) | Inspecter, rechercher, analyser et lire |
| Contrôle de débogage | Aussi désactiver/activer/surligner et capturer |
| Modification des shaders | Aussi compiler, remplacer et restaurer |

Changez le niveau dans le panneau MCP du standalone. Le client ne peut pas l’élever par appel d’outil. Consultez le journal d’activité, les refus et arguments pour comprendre une opération. Ces niveaux ne remplacent pas les modes de validation distincts de l’assistant intégré.

## Parcours type {#typical-workflow}
Demandez le graphe, suivez passes/ressources vers les shaders candidats, puis lisez liaisons et temps. La décompilation peut répondre en attente : réessayez après un délai plutôt que de supposer un résultat vide. L’état d’une commande nécessite une capture profonde terminée ; les temps nécessitent le bon niveau de suivi. Lisez les réserves, sources et confiances.

Un seul proxy utilise le pipe à la fois. Si la connexion échoue, vérifiez l’ouverture de l’application et l’absence d’un autre client qui garde la liaison. Les conversations externes appartiennent au client, pas à l’assistant intégré. Voir [l’inventaire des outils et le transport](doc:ref-mcp).


## Illustrations {#illustrations}

### Deux contrôles d’autorisation indépendants {#figure-0}

Les modes de l’assistant intégré et les niveaux d’outils MCP externes sont des réglages distincts. Le standalone contrôle les deux parcours.

```diagram
{"type":"flow","title":"Deux contrôles d’autorisation indépendants","nodes":[{"id":"assistant","label":"Assistant : Auto / Demander / Tout","x":0,"y":0,"width":280},{"id":"host","label":"Contrôles dans le standalone","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP : Lire / Déboguer / Modifier","x":640,"y":0,"width":280},{"id":"provider","label":"Fournisseur CLI / API","x":0,"y":160,"width":280},{"id":"actions","label":"Actions jeu / fichiers autorisées","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```

### Circulation des données {#figure-1}

ReShade charge l’observateur. Le standalone possède l’analyse, les fichiers et les décisions d’autorisation.

```diagram
{"type":"flow","title":"Circulation des données","nodes":[{"id":"game","label":"Jeu + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Mémoire partagée / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Analyse standalone","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profils / mods","x":600,"y":160,"width":280},{"id":"ai","label":"Fournisseur IA / agent MCP","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```
