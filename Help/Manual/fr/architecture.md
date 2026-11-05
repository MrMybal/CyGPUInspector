# Composants, fichiers et circulation des données

## Responsabilités
ReShade charge CyGPUInspectorRS dans le processus compatible. L’observateur transmet inventaires, commandes, captures bornées et acquittements par mémoire partagée et pipes nommés. Le standalone possède analyse, affichage, persistance, workers fournisseurs et contrôles d’autorisation. CyGPUInspectorMCP est un proxy local ; CyGPUInjector rejoue séparément les mods. Le plugin Unreal gère le chargement précoce et le viewport.

Le [schéma des échanges](figure:0) distingue processus du jeu et d’analyse. Les images GPU partagées empruntent un autre chemin : l’aperçu ouvre des textures compatibles plutôt que de transmettre tous les pixels dans le ring. L’export profond suit un parcours distinct de copie/lecture/écriture.

## Reconnaître les fichiers
| Emplacement | Rôle |
| --- | --- |
| App/Help | Aide hors ligne, langues, figures et références techniques |
| App/Lang | Catalogues de l’interface |
| CyGPUInspectorApp.settings.json | Préférences d’apparence, langue et fournisseur ; aucune clé API persistée |
| CyGPUInspectorApp.ini | Layout d’ancrage |
| Database | Bytecode, analyses en cache, tags et annotations |
| Games | Profil par jeu et état persistant |
| Captures | Frames sauvegardées et sorties profondes |
| Images | PNG d’aperçu sauvegardés individuellement |
| Dossiers .cygimod | Manifestes, bytecode et provenance source des mods |

Sauvegardez les dossiers entiers. Une analyse générée n’est pas une source originale du jeu. Contrôlez provenance et validation avant de réutiliser un résultat après changement de backend/modèle.

## Références développeur
L’index Référence technique contient les documents d’architecture, IPC, captures, partage/profilage GPU, shaders, ressources, base, IA/MCP, Unreal, limites et traduction. L’anglais est la base et les équivalents français sont fournis. Intégration source et dépendances restent séparées du guide par tâches.

Voir [Architecture](help:ref-Architecture), [IPC](help:ref-IPC) et [Intégration CyUI](help:ref-CyUIIntegration).
