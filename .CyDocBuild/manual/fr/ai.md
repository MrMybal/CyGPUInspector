---
id: ai
title: Utiliser l’assistant IA intégré
description: Connecter un fournisseur, poser une question précise et choisir les permissions.
section: Guide utilisateur
order: 12
sourceRevision: 557287e63b46
---

# Utiliser l’assistant IA intégré {#use-the-built-in-ai-assistant}

## Connecter un fournisseur {#connect-a-provider}
Ouvrez Assistant IA > Connexion. Choisissez Codex CLI, Claude Code CLI, une API compatible OpenAI ou une API locale. Les CLI utilisent leur connexion existante ; aucun modèle n’est embarqué. Pour une API, indiquez l’URL complète chat/completions, le modèle et la clé si nécessaire. HTTPS est requis hors endpoint local. La connexion peut émettre une petite requête ; l’usage dépend du fournisseur.

La clé reste en mémoire pour la session. Fournisseur/modèle/URL peuvent être conservés comme préférences. Un fournisseur distant reçoit les échanges et données d’outils demandées, éventuellement le code des shaders. Un modèle local garde ce parcours sur la machine si c’est votre objectif. Ne collez pas de clé dans le chat.

## Poser une question précise {#ask-a-specific-question}
Sélectionnez d’abord frame, passe ou shader. Demandez quelles passes contribuent au bloom, ce que calcule le shader choisi ou quels indices établissent une dépendance de profondeur. L’assistant utilise les mêmes outils hôtes que MCP et cite des identifiants et une confiance. Annuler la requête arrête le travail en cours. Nouvelle conversation efface le chat, pas les modifications du jeu.

## Choisir le mode de l’assistant {#choose-the-assistant-permission-mode}
| Mode | Appels / opérations par message | Validation |
| --- | --- | --- |
| Automatique | 8 / 1 | Dans le cadre de l’opération demandée |
| Demander à chaque fois | 8 / 1 | Chaque opération et envoi de code de shader |
| Accès complet | 16 / 8 | Sans question par opération |

Une opération change le jeu ou écrit une sortie ; lire structure/temps est différent. Changer de mode annule la demande en cours. Ces modes sont distincts des [niveaux MCP externes](doc:mcp). Voir [le schéma des autorisations](doc:ai#figure-0).

## Travaux sur les shaders {#shader-analysis-jobs}
Code du shader > Analyse IA propose nettoyage, classification et explication. Les sorties gardent modèle, provenance, version du prompt et confiance. Un nettoyage ouvre un HLSL reconstruit éditable ; compilez et contrôlez sa compatibilité avant application. Un tag suggéré n’est appliqué que par son action. Ni l’assistant ni un décompilateur ne récupèrent la source originale ; une explication plausible n’est pas une mesure.

Voir [la référence IA complète](doc:ref-ai), ses budgets, limites de contexte et snapshots.


## Illustrations {#illustrations}

### Deux contrôles d’autorisation indépendants {#figure-0}

Les modes de l’assistant intégré et les niveaux d’outils MCP externes sont des réglages distincts. Le standalone contrôle les deux parcours.

```diagram
{"type":"flow","title":"Deux contrôles d’autorisation indépendants","nodes":[{"id":"assistant","label":"Assistant : Auto / Demander / Tout","x":0,"y":0,"width":280},{"id":"host","label":"Contrôles dans le standalone","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP : Lire / Déboguer / Modifier","x":640,"y":0,"width":280},{"id":"provider","label":"Fournisseur CLI / API","x":0,"y":160,"width":280},{"id":"actions","label":"Actions jeu / fichiers autorisées","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```

### Parcours de modification d’un shader {#figure-1}

Conservez le bytecode original ; relisez et compilez la reconstruction avant de l’appliquer explicitement.

```diagram
{"type":"flow","title":"Parcours de modification d’un shader","nodes":[{"id":"inspect","label":"1. Inspecter le bytecode","x":0,"y":0,"width":280},{"id":"edit","label":"2. Reconstruire / éditer","x":300,"y":0,"width":280},{"id":"compile","label":"3. Compiler et vérifier","x":600,"y":0,"width":280},{"id":"apply","label":"4. Appliquer en direct","x":600,"y":160,"width":280},{"id":"verify","label":"5. Observer ou restaurer","x":300,"y":160,"width":280},{"id":"save","label":"6. Sauver profil / mod","x":0,"y":160,"width":280}],"edges":[{"from":"inspect","to":"edit"},{"from":"edit","to":"compile"},{"from":"compile","to":"apply"},{"from":"apply","to":"verify"},{"from":"verify","to":"save"}]}
```
