# IA

*Disponible aussi en [anglais](../AI.md), qui fait foi.*

**État : les fondations sont implémentées** (milestone 9) : étiquetage des sources, tags,
annotations persistantes et snapshot d'analyse. **Les jobs IA connectés à une API restent à
faire.**

## Position dans l'outil

Le standalone **n'embarque aucun modèle**. Deux chemins seulement :

1. **Agent externe via MCP** — le chemin principal, **implémenté** : l'IA navigue elle-même dans
   les données avec les outils décrits dans [MCP.md](MCP.md).
2. **Jobs assistés** — nettoyage de HLSL, classification, explication, lancés depuis l'interface
   avec une clé API fournie par l'utilisateur. **Pas encore implémenté** : le stockage, les
   étiquettes et le versionnement des prompts existent, l'appel HTTP non.

## Ce que l'IA fait, et à partir de quoi

Toujours à partir de données concrètes extraites par l'outil : désassemblage, pseudo-HLSL produit
par un vrai décompilateur, bindings, graphe de dépendances, timings.

* renommer variables et fonctions (`r0`, `r1` → `SceneColor`, `Exposure`, `Luminance`) ;
* commenter, identifier des algorithmes connus, reconstruire des structures ;
* deviner le rôle probable d'une ressource ;
* simplifier du code généré, expliquer des opérations mathématiques ;
* proposer un nom de passe et un rôle (`Likely Role: Tonemapping — Confidence 92 %`).

## Ce qui est implémenté

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
  vérification est possible (désactiver le shader et regarder l'image), l'outil la propose.
* L'IA ne remplace jamais un décompilateur : elle travaille après lui.
* Modèle et version du prompt sont stockés avec le résultat, pour pouvoir le régénérer (§58).

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
