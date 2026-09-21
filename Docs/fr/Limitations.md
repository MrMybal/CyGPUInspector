# Limites connues

*Disponible aussi en [anglais](../Limitations.md), qui fait foi.*

Ce fichier est vivant. Il est là pour que les limites soient **dites**, dans l'outil comme dans la
documentation, plutôt que prises pour des bugs.

## Périmètre

* CyGPUInspector ne fonctionne que là où **ReShade fonctionne normalement**. Aucun contournement
  d'anti-triche, de protection, de blocage d'injection ou de mécanisme de sécurité n'est développé,
  et aucun ne le sera. Une application qui refuse ReShade est **non supportée**, point.
* ReShade doit être une build **add-on enabled**. Les builds sans add-ons ne chargent aucun
  `.addon64`.
* Binaire 64 bits uniquement.

## Ce que l'API ReShade ne donne pas

* **Aucun nom de passe.** `PIXBeginEvent`, `ID3DUserDefinedAnnotation` et
  `vkCmdBeginDebugUtilsLabelEXT` ne sont pas exposés. Les noms de passe du frame graph seront donc
  toujours soit déduits, soit inférés par IA, soit donnés par l'utilisateur — jamais lus dans le
  jeu.
* **Aucun accès au source HLSL original.** Il n'existe pas dans le binaire. Tout HLSL affiché par
  l'outil est une reconstruction, et est étiqueté comme telle.
* Pas de désassembleur ni de réflexion intégrés : ce travail est fait côté standalone avec
  `d3dcompiler` et `dxcompiler`.
* `destroy_pipeline` n'est pas appelé en D3D9.

## Limites de l'implémentation actuelle (v0.1.0)

* Seule la **render target 0** est associée à chaque événement de draw. Le jeu complet des cibles
  est transporté par les événements de binding, mais limité aux **quatre premiers slots**.
* Les **lectures par SRV ne sont pas suivies** : elles passent par les événements de descripteurs,
  les plus coûteux de l'API. « Lecture » signifie aujourd'hui source de copie ou cible de
  profondeur.
* Le **niveau de tracking** contrôle la publication mais ne désenregistre pas encore les callbacks
  coûteux.
* `RuntimeControl` accélère la décision « ce pipeline est-il désactivé ? » par une table plate de
  131072 entrées. Un jeu qui créerait plus de pipelines que cela verrait les derniers échapper au
  chemin rapide.
* Le ring d'événements fait 64 Mio. Si le standalone ne consomme pas assez vite, des événements
  sont **abandonnés** (jamais mis en attente au détriment du jeu) et la perte est affichée.
* Une frame est plafonnée à 250 000 événements (`MaxEventsPerFrame`).

## Limites du frame graph (milestone 4, implémenté)

* Un **dispatch ne dit pas ce qu'il écrit** sans suivi des descripteurs : les passes compute sont
  regroupées par pipeline et leurs dépendances sont incomplètes. C'est la limite principale.
* Les noms de passe sont des **déductions**, avec une confiance affichée. Une passe non reconnue
  s'appelle `Unknown Pass #N` plutôt que de recevoir un nom inventé.
* La cible de profondeur d'un draw compte comme lue **et** écrite, parce que l'API ne dit pas si
  l'écriture de profondeur est activée. Un graphe peut donc montrer une dépendance de profondeur
  un peu plus large que la réalité.
* Le graphe est reconstruit quatre fois par seconde sur la dernière frame reçue ; il ne compare pas
  encore les frames entre elles.

## Limites de la chaîne de shaders (milestone 5, implémenté)

* `dxcompiler.dll` n'est pas une DLL système : sans elle, les shaders **Shader Model 6** ne peuvent
  être ni désassemblés ni réfléchis. L'onglet `Tools` dit où elle a été cherchée.
* Le désassemblage DXIL est de l'assembleur **LLVM**, pas la forme lisible du SM 5. C'est le format
  que produit DXC, pas un choix de notre part.
* La réflexion donne ce que le shader **déclare**, pas ce qui lui est réellement lié à un draw
  donné : cette correspondance demande le suivi des descripteurs.

## Limites du contrôle runtime (milestone 7, implémenté)

* Un pipeline dont un sous-objet n'est pas copiable n'est **pas remplaçable**. La raison est
  affichée plutôt que l'échec silencieux.
* Un draw remplacé coûte deux `bind_pipeline` de plus : c'est un mode de diagnostic.
* En D3D12, un shader DXIL de remplacement doit être signé (voir le piège `dxil.dll`).

## Limites du profiling GPU (milestone 8, implémenté)

* ReShade appelle l'add-on **avant** une commande, jamais après : le temps affiché pour un draw
  est l'intervalle jusqu'à la commande enregistrée suivante, pas la durée exacte du draw. C'est
  dit dans l'interface.
* Les résultats viennent de la frame N-3 et portent leur propre numéro de frame ; ils sont
  rapprochés de la frame affichée par index d'événement, ce qui est une approximation.
* Plafond de 16384 timestamps par frame ; au-delà les marques sont comptées comme abandonnées.
* En D3D12 multi-command-list, l'ordre d'enregistrement n'est pas l'ordre d'exécution.

## Limites des captures (milestone 11, implémenté)

* Une capture garde **une frame**, pas une séquence, et **aucun pixel** : les previews GPU
  disparaissent quand on l'ouvre. Tout le reste fonctionne.
* Seuls les shaders **utilisés dans la frame** sont archivés, avec leur bytecode. Les autres ne le
  sont pas : une capture est une unité d'analyse, pas un vidage de tout ce que le jeu a créé.
* Le format porte un numéro de version et refuse une capture écrite par une autre version, plutôt
  que de la lire de travers.
* La comparaison rapproche les shaders **par signature**, jamais par identifiant : un identifiant
  est local à une session.

## Limites de l'IA et du MCP (milestones 9 et 10)

* Le standalone **n'appelle aucune API d'IA** : le chemin implémenté est le MCP, où c'est un agent
  externe qui raisonne. Les jobs assistés avec clé API restent à faire.
* Le serveur MCP n'accepte **qu'un proxy à la fois**.
* Un outil qui demande un travail long le démarre et répond « redemande dans un instant » plutôt
  que de bloquer l'interface.
* Le niveau de permission est décidé dans le standalone et **n'est pas persisté** : il repart à
  `Read Only` à chaque lancement, délibérément.

## Limites structurelles de la décompilation

* Le HLSL reconstruit peut **ne pas recompiler**. C'est attendu ; l'état de validation fait partie
  des métadonnées de chaque backend.
* `cygi-dxbc` est registre par registre : il ne retrouve ni noms, ni expressions.
* `dxbc-spirv` est un compilateur, pas un traducteur : il réordonne et replie les instructions.
  Sa sortie est lisible mais ce n'est pas la forme qui a été écrite. Il **abandonne** sur un shader
  qui utilise des adresses de buffer (pointeurs physiques), parce que HLSL ne sait pas les exprimer.
* **`vkd3d-shader` n'est pas intégré et ne peut pas l'être avec cette chaîne d'outils** : sept de
  ses en-têtes sont générés par autoconf, widl, flex, bison et un script maison, et son code commun
  inclut `<pthread.h>` et `<unistd.h>`. Détail dans [ShaderDecompiler.md](ShaderDecompiler.md).
  `dxbc-spirv` occupe la place qui lui était destinée.
* `hlsldecompiler` (3Dmigoto) retrouve les noms des ressources, des constant buffers et de leurs
  membres, et reconstruit les expressions, mais **pas** les noms de variables locales, ni les
  structures, ni les algorithmes : le compilateur les a détruits.
* `dxil-spirv` passe par du SPIR-V Vulkan, qui ne transporte ni noms ni registres Direct3D. Les
  deux sont remis en place depuis les tables de réflexion du shader, mais **les membres d'un
  constant buffer sont perdus** : ils sont écrasés dans un tableau de `float4`. Les sémantiques
  d'interpolants deviennent `TEXCOORD<n>`, et le contrôle de flux est celui que le *structurizer* a
  reconstruit. C'est le prix du seul chemin existant vers le Shader Model 6.
* Le point d'entrée d'une reconstruction `dxil-spirv` est émis sous le nom `main`, pas sous son nom
  réel, pour que la validation par recompilation fonctionne. Le vrai nom est indiqué dans les notes.
* Le code vendorisé de 3Dmigoto date de 2014 et est compilé avec des options relâchées
  (`/permissive`, `/Zc:strictStrings-`, `/W0`) pour rester identique à l'amont. C'est un choix
  assumé : le corriger rendrait la conformité GPL invérifiable par simple `diff`.
* **DXIL perd plus d'information que DXBC** (inlining LLVM, SROA, vectorisation détruite) : la
  reconstruction SM 6.x sera systématiquement moins lisible que la SM 5.x. Ce n'est pas un défaut de
  l'outil.
* Une sortie IA est une **hypothèse**, accompagnée d'une confiance, jamais présentée comme un fait.
  Les trois niveaux `Original source` / `Decompiler reconstructed` / `AI reconstructed` sont
  toujours distingués.

## Limites des packages de mods (CyGPUInjector)

* Un **remplacement ne prend effet qu'à la création du pipeline**, donc en général au chargement du
  jeu. Activer un package en cours de partie depuis l'overlay ne change rien tant que le pipeline
  n'est pas recréé ; les suppressions de draw, elles, sont immédiates. L'échange à chaud est dans
  l'inspecteur, pas dans l'injecteur, qui est délibérément petit.
* Un package **cesse de s'appliquer** quand le jeu modifie réellement le shader. C'est voulu :
  l'appariement se fait sur le hash du code, donc un package périmé ne trouve rien plutôt que de
  toucher le mauvais shader.
* **Deux packages sur le même shader** : le premier par ordre alphabétique gagne, l'autre est
  ignoré avec un avertissement dans `ReShade.log`.
* CyGPUInjector **n'injecte rien** malgré son nom : c'est un add-on que ReShade charge, et les
  modifications passent par l'API add-on officielle. Un jeu qui refuse ReShade reste hors de portée.

## Limites des captures

* **Ni l'une ni l'autre ne rejoue la frame.** Un débogueur comme RenderDoc sérialise le flux de
  commandes et le ré-exécute sur son propre device ; c'est de là que viennent l'historique de
  pixel, le pas-à-pas sur un draw et la géométrie après vertex shader. CyGPUInspector observe la
  vraie frame via l'API add-on officielle de ReShade et n'injecte jamais de device (§2 du brief),
  donc ces quatre choses sont hors de portée. Voir [CaptureModes.md](CaptureModes.md).
* **Aucun nom de passe ne vient du jeu** : l'API add-on n'expose ni `PIXBeginEvent` ni
  `ID3DUserDefinedAnnotation`. Tous les noms sont dérivés et affichés avec leur confiance.
* Le contenu d'une **table de descripteurs** Direct3D 12 n'est pas lisible : elle est liée par
  handle et l'API ne donne pas le tas. La commande est marquée comme incomplète plutôt que de
  paraître ne rien lier.
* La capture profonde a un **plafond** de 60 000 commandes par frame et 256 descripteurs par
  commande. Au-delà, ce qui est perdu est compté et publié, pas masqué.
* Les liaisons posées **avant** l'armement d'une capture profonde ne sont pas connues d'elle. Elle
  démarre à une frontière de frame, ce qui suffit pour un jeu qui relie son état par frame, mais
  pas dans l'absolu pour le contexte immédiat Direct3D 11.
* La capture runtime n'a de **longueurs de barres** qu'au niveau `Pass Timing` ou plus. Sinon les
  passes sont listées dans l'ordre, avec des barres transparentes dimensionnées par le nombre de
  commandes — et le panneau le dit.

## Limites du partage GPU (milestone 3, implémenté)

* **Aucune synchronisation** pour l'instant : le standalone échantillonne la texture pendant que le
  jeu la remplit, donc un déchirement est possible sur une frame. La fence partagée est prévue mais
  pas branchée. C'est un compromis assumé : l'alternative serait une copie CPU, que le cahier des
  charges interdit dans le flux live.
* Les ressources **multi-échantillonnées** ne sont pas prévisualisées (il faudrait un resolve) :
  le statut `multisampled` est renvoyé.
* `max_width` / `max_height` sont ignorés : la texture est partagée en résolution native.
* En **D3D12**, les barrières supposent que la source est en `shader_resource` au moment du
  present. C'est le cas d'une render target ou d'une texture en fin de frame, mais cela reste une
  hypothèse. D3D11 n'est pas concerné.
* Une seule preview à la fois par session connectée.
* La preview lit le handle d'une ressource puis interroge sa description. Si le jeu détruit cette
  ressource depuis un autre thread exactement entre les deux, l'add-on utilise un handle périmé.
  La fenêtre est d'une poignée d'instructions et n'a pas été observée, mais elle existe : la
  fermer demande de retenir une référence sur la ressource, ce que l'API add-on n'expose pas.

## Limites de la localisation

* Une traduction ne vaut que ce que vaut son catalogue. Ce qui manque retombe sur l'anglais, ce qui
  est visible mais pas cassé ; **Affichage → Langue** indique combien de chaînes n'ont pas de
  traduction.
* Les lignes de journal, les messages d'erreur des add-ons et les noms d'outils MCP ne sont
  délibérément pas traduits — voir [Translating.md](Translating.md).
