# CyGPUInspectorMCP

## Forme

`CyGPUInspectorMCP.exe` utilise CyMCPSDK 0.7.0 pour MCP sur stdio et transmet les opérations au
standalone ouvert par son pipe nommé local. Le standalone possède l'état et les permissions ; les
requêtes s'exécutent sur son thread d'interface. Le proxy peut conserver des snapshots bornés de gros
résultats. Il ne s'accorde pas de permissions et n'expose pas de serveur HTTP.

Sans argument, ou avec `mcp`, il expose les 111 outils. `mcp-compact` expose cinq passerelles de
découverte et d'exécution, avec ressources pour les gros résultats. Le catalogue est paginé : suivez
`nextCursor` plutôt que de considérer le premier `tools/list` comme complet. Suivez `firstUri` et
`nextUri` pour lire les fragments de résultats. L'entrée JSON-RPC comporte un objet par ligne ;
stdout reste réservé au protocole. Le SDK traite initialisation, notifications, découverte, appels
d'outils et ressources compactes. Les protocoles incluent 2025-06-18, 2025-11-25 et le chemin de
découverte SDK 2026-07-28 ; les clients 2024 ne sont pas pris en charge par cette intégration.

Pour les scripts : `--help`, `list`, `describe NOM`, `invoke NOM JSON` ou
`invoke-file NOM FICHIER`. Le standalone doit être ouvert pour les appels applicatifs.
Voir les commandes et procédures de récupération du [guide d'automatisation](../Automation.md).

## Niveaux de permission

L'hôte démarre en lecture seule. Changez son niveau MCP externe dans l'interface ou lancez-le
explicitement avec `--mcp-permission=read-only|debug-control|shader-modification`. Le proxy ne peut
pas augmenter ce niveau. Ce réglage est indépendant des modes Auto/Demander/Complet de l'assistant.

| Niveau | Périmètre |
|---|---|
| Read Only | Frame, shaders, ressources, état applicatif, sélections et requêtes classées en lecture |
| Debug Control | Capture, débogage et mutations applicatives autorisés à ce niveau |
| Shader Modification | Compilation, remplacement, restauration et opérations exigeant le niveau maximal |

Chaque outil déclare sa permission précise. Utilisez `describe` ou la découverte, sans déduire
le niveau du nom. Les mutations sont journalisées dans le standalone. Les 34 définitions GPU sont
dans `McpTools.hpp` ; les 77 définitions applicatives dans `AutomationTools.hpp`.
Les clés et l'attribution de permissions ne sont pas des outils appelables.

## Pourquoi ces outils

Partez des preuves : graphe de frame, lecteurs d'une ressource HDR, shaders de réduction et de flou,
puis identifiants, timings et confiance. Avec la permission requise, désactivez un shader et observez
le jeu pour vérifier l'hypothèse. Les outils applicatifs peuvent sélectionner ce shader, ouvrir
l'éditeur et préserver un brouillon avant édition ; ils partagent les handlers de l'interface.

## Réponses

Les résultats identifient shaders, ressources et commandes. Noms de passes dérivés, HLSL reconstruit
et intervalles de timing conservent leurs réserves : ni source originale ni preuve du rendu visuel.
Les réponses applicatives utilisent `{ok,result}` ou un échec structuré. MCP signale les échecs par
`isError`. Le CLI retourne 0 en cas de réussite, 1 pour un échec métier, 2 pour une entrée invalide
ou une erreur de transport. Les gros résultats sont des snapshots immuables lus par fragments
(32 Kio par défaut). Ils expirent après cinq minutes par défaut ; le stockage est borné à 64 Mio et
128 résultats. Suivez les liens avant expiration ; les identifiants ne survivent pas au redémarrage.

## Limites

* Désassemblage et décompilation peuvent lancer un travail asynchrone : interrogez à nouveau ensuite.
* Un seul proxy local peut occuper le pipe à la fois.
* Les timings exigent le profiling ; l'état de commande exige une capture profonde. Les prérequis
  manquants sont signalés.
* Les mutations ne sont ni mises en cache ni rejouées automatiquement. Annuler ne les défait pas.
* Réglages optionnels : `CYGI_MCP_CACHE_TTL_MS` (30000), `CYGI_MCP_RESULT_TTL_MS` (300000),
  `CYGI_MCP_RESULT_BYTES` (67108864), `CYGI_MCP_CHUNK_BYTES` (32768).
  `CYGI_MCP_METRICS_FILE` active un journal privé de performances limité aux métadonnées, désactivé par défaut.
* Catalogues statiques et snapshots immuables n'exigent pas d'abonnement aux changements.
