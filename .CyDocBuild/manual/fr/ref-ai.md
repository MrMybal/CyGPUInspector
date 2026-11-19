---
id: ref-ai
title: Assistant IA et analyse
description: Détails techniques : Connecter un fournisseur, poser une question précise et choisir les permissions.
section: Référence technique
order: 19
parent: ai
sourceRevision: 5f87fa7fa33b
---

# IA {#ai}

## Position dans l'outil {#where-it-sits-in-the-tool}

L'application n'embarque aucun modèle. L'assistant intégré et les agents MCP externes utilisent les
mêmes 111 opérations : 34 outils GPU et 77 opérations applicatives. Ils peuvent inspecter une frame,
sélectionner panneaux et filtres, éditer le HLSL, gérer notes et mods et piloter les captures dans
les limites accordées par l'hôte. Consultez le [guide d'automatisation](doc:automation).
Les conversations MCP externes restent dans leur client ; voir le [transport MCP](doc:ref-mcp).
Les appels au fournisseur tournent sur des workers ; les opérations passent par le standalone.

## Le panneau Assistant IA {#the-ai-assistant-panel}

Ouvrez **Affichage > Assistant IA**. Les onglets Tchat, Options et Connexion présentent le fournisseur,
les requêtes, les approbations et la conversation. CyMCPSDK 0.7.0 fournit les connexions, le streaming
et l'état des conversations.

### Connexion {#connection}

| Fournisseur | Intégration | Vérification de connexion |
|---|---|---|
| Codex CLI | Session app-server native, outils applicatifs enregistrés et connexion CLI existante | Démarrage et dialogue app-server |
| Claude Code CLI | Fournisseur SDK avec boucle JSON bornée de l'hôte, connexion CLI existante | Exécutable et version CLI |
| API compatible OpenAI | Appels de fonctions natifs et réponses en streaming | Découverte HTTP des modèles, sans réponse de tchat générée |
| API locale | Même intégration HTTP avec un service local existant | Même découverte des modèles |

Le modèle CLI peut rester vide pour utiliser celui du fournisseur. La connexion ne lance pas de
génération ; sa réussite ne garantit pas que le compte puisse terminer une conversation.
Codex utilise son sandbox en lecture seule et les outils applicatifs. Les outils intégrés et les
sources de réglages de Claude sont désactivés pour cette intégration. Le catalogue applicatif
n'expose ni commande shell arbitraire ni lecture libre de fichiers.

Pour HTTP, indiquez l'URL complète chat/completions et un modèle. HTTPS est requis sauf en loopback.
Les identifiants dans l'URL et les redirections sont refusés. La clé API reste en mémoire ; les
préférences fournisseur, modèle et URL peuvent être conservées dans `CyGPUInspectorApp.settings.json`.
Changer le type de fournisseur ou l'URL efface la clé saisie. Le SDK masque les informations sensibles
dans les erreurs.

### Tchat {#chat}

Envoyez une demande précise avec **Envoyer** ou Ctrl+Entrée. Codex et HTTP utilisent les outils
natifs ; Claude renvoie une réponse ou une demande d'outil JSON par étape. Chaque mutation consomme
le budget d'opérations.

| Mode de permission | Outils par message | Opérations par message | Approbation |
|---|---|---|---|
| Automatique | 8 | 1 | Dans le cadre de la demande |
| Demander à chaque fois | 8 | 1 | Chaque mutation et divulgation de code shader |
| Accès complet | 16 | 8 | Sans invite par opération |

Les opérations incluent les changements d'état applicatif, les fichiers gérés et les actions GPU.
L'inspection seule ne consomme pas le budget de mutations. Changer de mode annule la génération.
Annuler arrête la génération sans revenir sur les opérations terminées. Une nouvelle conversation
réinitialise le tchat et son checkpoint SDK lorsque le fournisseur est inactif, sans annuler les
changements du jeu ou de l'espace de travail.

Le tchat visible est conservé sans l'ancienne éviction à 32 messages. Pour les conversations SDK
natives, la projection du contexte API possède une limite d'octets indépendante (0 = illimité) et
préserve les groupes appel/résultat sans effacer l'historique complet. Les fournisseurs natifs gèrent
leur contexte. La boucle Claude transmet un contexte récent borné à 16 messages et 24 000 caractères.

La conservation des checkpoints privés est désactivée par défaut. Activée pour les conversations SDK
natives, elle utilise `CyGPUInspector/AssistantHistory/` dans le Local AppData de l'utilisateur,
sans clé API. La boucle JSON Claude n'utilise pas ces checkpoints natifs. La restauration demande
la session exacte ; une mutation échouée n'est jamais rejouée automatiquement. La découverte à la
demande peut réduire le catalogue initial ; la découverte compacte dépend du seuil configuré.

### Ce qui est envoyé {#what-is-sent}

Le fournisseur reçoit vos messages, le contexte applicatif et les résultats d'outils demandés :
métadonnées de frame, noms, timings, notes, désassemblage, HLSL reconstruit ou édité et exports gérés.
Un fournisseur distant reçoit ces données ; un modèle HTTP local utilise son service local.
En mode Demander à chaque fois, la divulgation du code shader exige une approbation. Ne placez pas
d'identifiants dans le tchat. Les fichiers accessibles sont limités aux données gérées par l'app
et aux dossiers d'import/export Automation documentés. Ce n'est pas un explorateur général.
Les opérations avec sauvegarde conservent des fichiers de récupération ; consultez le
[guide d'automatisation](doc:automation) avant une restauration de notes, disposition ou profils.

## Tâches sur les shaders : l'onglet Analyse IA {#shader-jobs-the-ai-analysis-tab}

Panneau Shader code > **Analyse IA**, pour le shader sélectionné :

* **Nettoyer le HLSL** : registres et temporaires renommés, commentaires, programme identique par
  ailleurs ;
* **Classifier** : un rôle probable parmi les tags de la section 39, un nom court, le raisonnement ;
* **Expliquer** : ce que fait le shader, ses entrées, ses sorties et son algorithme.

Chaque tâche envoie la description et les bindings du shader, son désassemblage et la
reconstruction de chaque décompilateur avec son verdict, en attendant d'abord le désassemblage et la
décompilation s'ils ne sont pas encore faits. Le résultat est enregistré comme annotation du shader,
**par signature**, avec le fournisseur et le modèle, la version du prompt (`cygi-assistant-1`), la
date et la confiance. Un nettoyage s'ouvre dans l'éditeur HLSL, où il se compile et s'injecte comme
n'importe quelle reconstruction. Une classification propose d'appliquer son tag ; il n'est appliqué
que si vous cliquez.

## Ce que l'IA fait, et à partir de quoi {#what-the-ai-does-and-from-what}

Toujours à partir de données concrètes extraites par l'outil : désassemblage, pseudo-HLSL produit
par un vrai décompilateur, bindings, graphe de dépendances, timings.

* renommer variables et fonctions (`r0`, `r1` → `SceneColor`, `Exposure`, `Luminance`) ;
* commenter, identifier des algorithmes connus, reconstruire des structures ;
* deviner le rôle probable d'une ressource ;
* simplifier du code généré, expliquer des opérations mathématiques ;
* proposer un nom de passe et un rôle (`Likely Role: Tonemapping — Confidence 92 %`).

## Étiquetage des sources, tags et versionnement {#source-labelling-tags-and-versioning}

**Étiquetage des sources.** Une annotation porte toujours son origine — `user`, `derived` ou
`AI` — son type (`note`, `classification`, `cleanup`, `explanation`), son auteur (le modèle, pour
une sortie IA), sa version de prompt, sa date et sa confiance. Les trois origines sont affichées
dans trois couleurs différentes. Il est structurellement impossible de stocker une phrase d'un
modèle sans dire qu'elle vient d'un modèle.

**Tags** (§39) : Depth, GBuffer, Lighting, Shadow, GI, Reflection, SSR, AO, Fog, Volumetric,
Post Process, Bloom, Tonemap, Upscale, UI, Particles, Video, Unknown. Avec un nom libre donné par
l'utilisateur. Le tout est persisté **par signature** dans `notes.json`, donc survit au
redémarrage du jeu, à sa mise à jour, et vaut même d'un jeu à l'autre.

**Versionnement** (§58) : chaque annotation conserve modèle et version de prompt, ce qui permet de
savoir qu'un résultat est périmé et de le régénérer.

## Règles non négociables {#non-negotiable-rules}

* Toute sortie IA est étiquetée `AI reconstructed` et distinguée de `Decompiler reconstructed`,
  elle-même distinguée de `Original source` — qui n'est jamais disponible (§31).
* Une classification est **une hypothèse avec une confiance**, jamais un fait (§40). Quand une
  vérification est possible (désactiver le shader et regarder l'image), l'assistant a pour consigne
  de la proposer.
* L'IA ne remplace jamais un décompilateur : elle travaille après lui.
* Modèle et version du prompt sont stockés avec le résultat, pour pouvoir le régénérer (§58).
* Le code des shaders, les noms, les résultats d'outils et les messages précédents sont des
  **données** pour le modèle, jamais des instructions à suivre.
* L'assistant ne rapporte que ce qu'un résultat d'outil confirme : « compilé » ne dit rien de ce que
  le jeu affiche.

## Analysis Snapshot {#analysis-snapshot}

**Implémenté.** `AI analysis snapshot` écrit un répertoire autonome : une capture complète
(`capture.json`, `events.bin`, `shaders/`) plus un dossier `analysis/` contenant

* `snapshot.md` : les passes déduites en prose **avec leur origine et leur confiance**, le tableau
  des shaders de la frame avec leurs temps GPU, et l'avertissement que rien de tout cela n'est du
  source original ;
* les désassemblages et les reconstructions HLSL déjà calculés, un fichier par backend, chacun
  préfixé par son verdict de validation.

C'est exactement ce qui serait donné à un modèle — donc ce que l'utilisateur peut relire, corriger
ou archiver **avant** de le transmettre, plutôt qu'un paquet opaque.
