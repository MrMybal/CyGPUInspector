# Utiliser l’assistant IA intégré

## Connecter un fournisseur
Ouvrez Assistant IA > Connexion. Choisissez Codex CLI, Claude Code CLI, une API compatible OpenAI ou une API locale. Les CLI utilisent leur connexion existante ; aucun modèle n’est embarqué. Pour une API, indiquez l’URL complète chat/completions, le modèle et la clé si nécessaire. HTTPS est requis hors endpoint local. La connexion peut émettre une petite requête ; l’usage dépend du fournisseur.

La clé reste en mémoire pour la session. Fournisseur/modèle/URL peuvent être conservés comme préférences. Un fournisseur distant reçoit les échanges et données d’outils demandées, éventuellement le code des shaders. Un modèle local garde ce parcours sur la machine si c’est votre objectif. Ne collez pas de clé dans le chat.

## Poser une question précise
Sélectionnez d’abord frame, passe ou shader. Demandez quelles passes contribuent au bloom, ce que calcule le shader choisi ou quels indices établissent une dépendance de profondeur. L’assistant utilise les mêmes outils hôtes que MCP et cite des identifiants et une confiance. Annuler la requête arrête le travail en cours. Nouvelle conversation efface le chat, pas les modifications du jeu.

## Choisir le mode de l’assistant
| Mode | Appels / opérations par message | Validation |
| --- | --- | --- |
| Automatique | 8 / 1 | Dans le cadre de l’opération demandée |
| Demander à chaque fois | 8 / 1 | Chaque opération et envoi de code de shader |
| Accès complet | 16 / 8 | Sans question par opération |

Une opération change le jeu ou écrit une sortie ; lire structure/temps est différent. Changer de mode annule la demande en cours. Ces modes sont distincts des [niveaux MCP externes](help:mcp). Voir [le schéma des autorisations](figure:0).

## Travaux sur les shaders
Code du shader > Analyse IA propose nettoyage, classification et explication. Les sorties gardent modèle, provenance, version du prompt et confiance. Un nettoyage ouvre un HLSL reconstruit éditable ; compilez et contrôlez sa compatibilité avant application. Un tag suggéré n’est appliqué que par son action. Ni l’assistant ni un décompilateur ne récupèrent la source originale ; une explication plausible n’est pas une mesure.

Voir [la référence IA complète](help:ref-AI), ses budgets, limites de contexte et snapshots.
