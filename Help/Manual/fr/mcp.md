# Connecter un agent externe avec MCP

## Configurer le client
Lancez d’abord le standalone. Configurez le client MCP pour exécuter App/CyGPUInspectorMCP.exe en transport stdio avec son vrai chemin d’installation. Le proxy transmet le JSON-RPC ligne par ligne au pipe local de l’application ; celle-ci possède session, données et autorisations. Le proxy ne capture pas un jeu et ne s’accorde pas de droits.

```json
{"mcpServers":{"CyGPUInspector":{"command":"C:/Tools/CyGPUInspector/App/CyGPUInspectorMCP.exe"}}}
```

Le chemin est un exemple à remplacer. La syntaxe exacte dépend du client. Connectez un producteur ou ouvrez une sauvegarde, puis demandez à l’agent d’inspecter session/frame avant une analyse coûteuse.

## Choisir le niveau d’outils externe
| Niveau | Capacités |
| --- | --- |
| Lecture seule (défaut) | Inspecter, rechercher, analyser et lire |
| Contrôle de débogage | Aussi désactiver/activer/surligner et capturer |
| Modification des shaders | Aussi compiler, remplacer et restaurer |

Changez le niveau dans le panneau MCP du standalone. Le client ne peut pas l’élever par appel d’outil. Consultez le journal d’activité, les refus et arguments pour comprendre une opération. Ces niveaux ne remplacent pas les modes de validation distincts de l’assistant intégré.

## Parcours type
Demandez le graphe, suivez passes/ressources vers les shaders candidats, puis lisez liaisons et temps. La décompilation peut répondre en attente : réessayez après un délai plutôt que de supposer un résultat vide. L’état d’une commande nécessite une capture profonde terminée ; les temps nécessitent le bon niveau de suivi. Lisez les réserves, sources et confiances.

Un seul proxy utilise le pipe à la fois. Si la connexion échoue, vérifiez l’ouverture de l’application et l’absence d’un autre client qui garde la liaison. Les conversations externes appartiennent au client, pas à l’assistant intégré. Voir [l’inventaire des outils et le transport](help:ref-MCP).
