# IA

*Disponible aussi en [anglais](../AI.md), qui fait foi.*

**État : implémenté** (milestone 9) : l'assistant IA intégré au standalone, ses tâches sur les
shaders, l'étiquetage des sources, les tags, les annotations persistantes et le snapshot d'analyse.
Le chemin externe, un agent connecté par MCP, est implémenté lui aussi.

## Position dans l'outil

Le standalone **n'embarque aucun modèle**. Un modèle atteint CyGPUInspector par l'un de deux
chemins, qui utilisent les mêmes outils, traités par le même code :

1. **Le panneau Assistant IA**, intégré au standalone. Vous choisissez un fournisseur, vous vous
   connectez, et vous posez vos questions dans le tchat. L'assistant travaille sur la frame avec les
   outils de CyGPUInspector, dans une boucle bornée, selon le mode d'autorisation que vous choisissez.
2. **Un agent externe via MCP** : l'agent tourne dans sa propre application et appelle les outils
   décrits dans [MCP.md](MCP.md). Sa conversation reste dans cette application.

L'assistant est conçu comme celui de CyRSAssistant, l'assistant ReShade du même auteur : mêmes
fournisseurs, même connexion, même boucle, mêmes limites, mêmes modes d'autorisation. Ici, le
standalone est déjà un processus à part, hors du jeu : ce que CyRSAssistant fait dans un service
compagnon tourne dans des threads du standalone. Les appels au fournisseur ne bloquent jamais
l'interface, et le jeu n'y est jamais mêlé.

## Le panneau Assistant IA

`Affichage > Assistant IA`. Trois onglets : **Tchat**, **Options**, **Connexion**. La ligne d'état
indique `PRÊT`, `CONNEXION EN COURS`, `ERREUR DE CONNEXION` ou `DÉCONNECTÉ`, avec la raison.

### Connexion

| Fournisseur | Ce qu'il utilise | Ce que Connecter vérifie |
|---|---|---|
| Codex (session CLI) | la CLI `codex` et sa connexion existante | `codex login status`, sans appeler le modèle |
| Claude Code (session CLI) | la CLI `claude` et sa connexion existante | `claude auth status`, sans appeler le modèle |
| API compatible OpenAI | une URL `/chat/completions` complète, un modèle, et une clé si le serveur en demande une | une petite requête à laquelle le modèle doit répondre `{"connected":true}` |
| Modèle local (API) | la même chose, sur `localhost` (Ollama, LM Studio, serveur llama.cpp) | la même chose |

* Le modèle peut rester vide pour une CLI : c'est alors le modèle par défaut de la CLI.
* Une CLI tourne dans un dossier temporaire à elle, sans projet autour. Codex tourne en lecture
  seule, sans shell et sans votre `config.toml`. Claude Code tourne sans outils, sans serveur MCP,
  sans hooks et sans session enregistrée. La CLI ne sert qu'à répondre : elle ne peut agir sur rien.
* Une API demande HTTPS, sauf sur `localhost`. Les identifiants dans l'URL et les redirections sont
  refusés : la clé ne part jamais ailleurs que vers le serveur indiqué. L'API doit accepter
  `response_format: {"type":"json_object"}`.
* Le fournisseur, le modèle et l'URL sont enregistrés dans `CyGPUInspectorApp.settings.json`. **La
  clé ne l'est pas** : elle reste en mémoire le temps de la session.
* La sortie d'erreur d'une CLI n'est jamais affichée, car elle peut contenir des informations de
  compte. Seul son code de sortie l'est.

### Tchat

Écrivez une question et cliquez sur **Envoyer** (Ctrl+Entrée). L'assistant répond par un objet JSON à
la fois : soit sa réponse, soit un outil à appeler. CyGPUInspector exécute l'outil, lui renvoie le
résultat, et ainsi de suite, dans un budget :

| Mode d'autorisation | Appels d'outils par message | Opérations par message | Autorisations |
|---|---|---|---|
| Automatique (par défaut) | 8 | 1 | aucune au-delà de votre demande |
| À la demande | 8 | 1 | chaque opération, et chaque code de shader envoyé au fournisseur |
| Accès complet | 16 | 8 | aucune |

Les *opérations* sont les outils qui modifient le jeu en cours ou écrivent des fichiers : désactiver,
réactiver, mettre en évidence, restaurer, capturer, capture profonde, compiler, remplacer. Lire la
frame, les passes, les ressources et les timings n'est jamais une opération. En mode **À la
demande**, la requête affiche **Autoriser cette action** / **Refuser cette action** avec les
arguments exacts (le HLSL d'un remplacement, par exemple). Aucun mode ne donne accès à un terminal ni
à vos fichiers. Changer de mode annule la demande en cours.

Le contexte fourni à chaque étape : votre demande, les derniers échanges (16 messages,
24 000 caractères), la session et la frame en mémoire, ce qui est sélectionné dans l'interface
(shader, ressource, commande), les outils avec leur description, les résultats obtenus et le budget
restant. La réponse arrive dans la langue de l'interface. Les 32 derniers messages restent en
mémoire ; **Nouvelle conversation** les efface sans rien annuler dans le jeu. **Annuler la demande**
l'arrête aussitôt : une CLI est terminée avec tous les processus qu'elle a lancés, une réponse HTTP
tardive est ignorée.

Les exemples sous le tchat reprennent les questions du cahier des charges (§84) : *quelle partie du
rendu semble produire le bloom*, *expliquer le pipeline*, *que fait le shader sélectionné*.

### Ce qui est envoyé

Vos messages et ce que l'assistant lit avec ses outils : la structure de la frame, les noms, les
timings et, quand il les demande, le désassemblage et le HLSL décompilé des shaders. Jamais vos
fichiers. Avec une CLI ou une API distante, ces données partent chez ce fournisseur ; avec un modèle
local, elles restent sur la machine. Le mode **À la demande** fait autoriser chaque code de shader
avant son envoi.

## Tâches sur les shaders : l'onglet Analyse IA

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

## Ce que l'IA fait, et à partir de quoi

Toujours à partir de données concrètes extraites par l'outil : désassemblage, pseudo-HLSL produit
par un vrai décompilateur, bindings, graphe de dépendances, timings.

* renommer variables et fonctions (`r0`, `r1` → `SceneColor`, `Exposure`, `Luminance`) ;
* commenter, identifier des algorithmes connus, reconstruire des structures ;
* deviner le rôle probable d'une ressource ;
* simplifier du code généré, expliquer des opérations mathématiques ;
* proposer un nom de passe et un rôle (`Likely Role: Tonemapping — Confidence 92 %`).

## Étiquetage des sources, tags et versionnement

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

## Règles non négociables

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

## Analysis Snapshot

**Implémenté.** `AI analysis snapshot` écrit un répertoire autonome : une capture complète
(`capture.json`, `events.bin`, `shaders/`) plus un dossier `analysis/` contenant

* `snapshot.md` : les passes déduites en prose **avec leur origine et leur confiance**, le tableau
  des shaders de la frame avec leurs temps GPU, et l'avertissement que rien de tout cela n'est du
  source original ;
* les désassemblages et les reconstructions HLSL déjà calculés, un fichier par backend, chacun
  préfixé par son verdict de validation.

C'est exactement ce qui serait donné à un modèle — donc ce que l'utilisateur peut relire, corriger
ou archiver **avant** de le transmettre, plutôt qu'un paquet opaque.
