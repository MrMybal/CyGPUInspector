---
id: mcp
title: Connecter un agent externe avec MCP
description: Connecter un agent externe et choisir les accès autorisés par le standalone.
section: Guide utilisateur
order: 13
parent: ai
sourceRevision: 98491ad2ba6b
---

# Connecter un agent externe avec MCP {#connect-an-external-agent-with-mcp}

## Configurer le client {#configure-the-client}

Lancez le standalone et ouvrez son panneau **MCP**. Le MCP HTTP local est activé par défaut
à `http://127.0.0.1:41823/mcp` (le panneau affiche et copie l'adresse effective). Avec le profil
**Free** par défaut, indiquez cette seule adresse dans un client MCP HTTP : aucun jeton ni
confirmation n'est demandé pour appeler les 111 opérations enregistrées. Les arguments et
prérequis métier restent validés. L'écoute reste en loopback. La case permet de désactiver HTTP.

Les clients stdio peuvent toujours lancer `App/CyGPUInspectorMCP.exe` avec son chemin installé :

```json
{"mcpServers":{"CyGPUInspector":{"command":"C:/Tools/CyGPUInspector/App/CyGPUInspectorMCP.exe"}}}
```

Remplacez le chemin d'exemple par votre installation. Le compagnon transmet les appels à la
même application ouverte ; il ne capture pas le jeu lui-même. Connectez un producteur ou ouvrez
une frame enregistrée pour les opérations qui exigent des données de capture.

## Choisir le niveau d’outils externe {#select-the-external-tool-level}

Le panneau MCP propose quatre profils :

| Profil | Jeton HTTP | Opérations |
| --- | --- | --- |
| Free (défaut) | Aucun | Toutes les opérations, sans confirmation |
| Local contrôlé | Aucun | Niveau de permission de l'hôte |
| Jeton seul | Requis | Toutes les opérations, sans confirmation |
| Protégé | Requis | Niveau de permission de l'hôte |

Les profils contrôlés proposent Read Only, Debug Control et Shader Modification. Le client
ne peut pas augmenter ses droits. Copiez le jeton de session dans le panneau pour les profils
avec jeton : généré en mémoire, il change au redémarrage et n'est jamais conservé dans les
préférences. Nouveau jeton révoque le précédent. Les jetons concernent HTTP ; le pipe stdio/CLI
conserve son transport local et respecte les niveaux des profils contrôlés. Le tchat intégré
garde ses modes Auto/Demander/Complet séparés.

Sans choix explicite, les préférences migrent vers Free. Profil choisi, HTTP désactivé, port et
réglages sans rapport sont conservés. Un niveau de permission explicitement enregistré dans
l'ancien format migre vers Local contrôlé. Un profil explicite inconnu est conservé et désactive
HTTP jusqu'à correction.

## Parcours type {#typical-workflow}
Demandez le graphe, suivez passes/ressources vers les shaders candidats, puis lisez liaisons et temps. La décompilation peut répondre en attente : réessayez après un délai plutôt que de supposer un résultat vide. L’état d’une commande nécessite une capture profonde terminée ; les temps nécessitent le bon niveau de suivi. Lisez les réserves, sources et confiances.

Un seul proxy utilise le pipe à la fois. Si la connexion échoue, vérifiez l’ouverture de l’application et l’absence d’un autre client qui garde la liaison. Les conversations externes appartiennent au client, pas à l’assistant intégré. Voir [l’inventaire des outils et le transport](doc:ref-mcp).


## Commandes terminal et transport SDK {#terminal-sdk}
Le [guide de pilotage applicatif](doc:automation) décrit les 111 opérations partagées :
connexions, disposition, réglages, aperçus, notes, HLSL, mods et commandes de l’assistant.

CyMCPSDK 0.7.2 fournit désormais le registre natif et le transport stdio. Sans argument,
l’exécutable démarre toujours MCP. `mcp-compact` propose la découverte à la demande et des
résultats privés par morceaux ; `mcp` conserve la découverte complète. L’initialisation accepte
2025-11-25 et 2025-06-18, pas 2024-11-05.

```powershell
.\CyGPUInspectorMCP.exe list
.\CyGPUInspectorMCP.exe describe inspect_shader
.\CyGPUInspectorMCP.exe invoke get_session '{}'
.\CyGPUInspectorMCP.exe invoke list_shaders '{"limit":3}'
.\CyGPUInspectorMCP.exe invoke-file inspect_shader args.json
```

Le fichier d’arguments contient un objet comme `{"shader_id":2}`. La découverte fonctionne sans
interface ; l’exécution s’adresse au standalone ouvert. JSON sur stdout, diagnostics sur stderr.
Codes de sortie : 0 réussite, 1 refus métier, 2 saisie invalide ou connexion impossible.
CLI et MCP appliquent le même profil d'accès. Sans profil choisi explicitement, les options
`--mcp-permission=read-only`, `--mcp-permission=debug-control` ou
`--mcp-permission=shader-modification` sélectionnent un accès contrôlé pour ce processus.
Free est le défaut ; un profil explicitement choisi reste prioritaire.
`--connect=<pid>` et les options de suivi et d’affichage sont conservés.

En mode compact, lisez toutes les URI de morceaux pour récupérer un résultat complet.
Rétention : cinq minutes en mémoire, 64 Mio / 128 résultats. Une référence expirée produit une
erreur ; ne rejouez jamais une modification pour récupérer sa sortie. Aucun dossier de recherche
dans les fichiers n’est exposé.

## Illustrations {#illustrations}

### Deux contrôles d’autorisation indépendants {#figure-0}

Les modes de l’assistant intégré et les niveaux d’outils MCP externes sont des réglages distincts. Le standalone contrôle les deux parcours.

```diagram
{"type":"flow","title":"Deux contrôles d’autorisation indépendants","nodes":[{"id":"assistant","label":"Assistant : Auto / Demander / Tout","x":0,"y":0,"width":280},{"id":"host","label":"Contrôles dans le standalone","x":320,"y":0,"width":280},{"id":"mcp","label":"Profils d'accès MCP","x":640,"y":0,"width":280},{"id":"provider","label":"Fournisseur CLI / API","x":0,"y":160,"width":280},{"id":"actions","label":"Actions jeu / fichiers autorisées","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```

### Circulation des données {#figure-1}

ReShade charge l’observateur. Le standalone possède l’analyse, les fichiers et les décisions d’autorisation.

```diagram
{"type":"flow","title":"Circulation des données","nodes":[{"id":"game","label":"Jeu + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Mémoire partagée / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Analyse standalone","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profils / mods","x":600,"y":160,"width":280},{"id":"ai","label":"Fournisseur IA / agent MCP","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```
