# CyGPUInspectorDatabase

*Disponible aussi en [anglais](../Database.md), qui fait foi.*

**État : implémenté et testé** : le magasin de shaders par signature (`ShaderStore`), les tags et
annotations (`ShaderNotes`), et un profil par jeu avec son état persistant (`GameProfile`).

## Principe

Tout est sur le disque, dans des fichiers lisibles : du JSON pour ce qui décrit, les fichiers
eux-mêmes pour les gros blobs. Aucun moteur de base de données à installer ou à réparer, et tout peut
être consulté, comparé ou modifié à la main. Rien de coûteux n'est recalculé deux fois : ce qui est
dérivé d'un shader est mis en cache **par signature de shader**, donc reconnu d'un lancement de jeu à
l'autre, et même d'un jeu à l'autre.

```
Database/
└── shaders/<aa>/<signature>/
    ├── metadata.json            étage, API, shader model, tailles, première et dernière apparition
    ├── original.dxbc | .dxil
    ├── disassembly.txt
    ├── decompiled/<backend>-<version>.hlsl   un fichier par backend, jamais écrasé
    ├── decompiled/<backend>-<version>.json   verdict de validation
    └── notes.json               nom, tags et annotations (utilisateur, déduites, IA), chacune avec son origine

Games/<exécutable>/
    ├── profile.json             le jeu, ses sessions, chaque shader vu dans ce jeu
    └── state.cygimod/           les shaders laissés désactivés ou remplacés (section 38)
```

Le premier niveau `<aa>` correspond aux deux premiers caractères hexadécimaux de la signature : il
évite qu'un même dossier contienne des milliers d'entrées. `Database/` et `Games/` sont à côté de
l'exécutable, comme les captures : l'outil reste portable.

## Ce qui est mis en cache

Désassemblage, décompilation (par backend), nettoyage IA, classification et explication IA, tags et
noms donnés par l'utilisateur.

## Versionnement

Chaque artefact dérivé porte `{ outil, version de l'outil, date, modèle IA, version du prompt }`
(§58). C'est ce qui permet de savoir qu'un résultat est périmé et de le régénérer, plutôt que de tout
recalculer à chaque ouverture ou de garder indéfiniment la sortie d'un outil obsolète.

## Reconnaître un shader déjà vu

1. Même `signature` → le même shader à l'octet près, tout le cache s'applique.
2. Sinon même `semantic_hash` → le même code avec des parties de debug différentes : le cache
   s'applique, et le dit.
3. Sinon : un nouveau shader.

## Profils par jeu (§63)

Un profil s'ouvre tout seul quand un jeu se connecte, ou quand une de ses captures est ouverte, dans
`Games/<exécutable>/`. `profile.json` contient :

* l'exécutable, l'API graphique et la carte graphique, la première et la dernière session, le nombre
  de sessions ;
* chaque shader envoyé par le jeu, par signature : son hash sémantique, son étage, son format et son
  shader model, sa première et sa dernière apparition, et dans combien de sessions.

Le panneau **Profil du jeu** l'affiche. Pour la session en cours, il indique combien de shaders sont
nouveaux, combien ont déjà été vus dans une session précédente et combien ont le même code qu'un
shader connu ; la liste des shaders marque les nouveaux. Il liste aussi les captures de ce jeu.
L'outil MCP `get_game_profile` donne la même chose à un agent, et à l'assistant IA : *qu'est-ce qui
est nouveau depuis la dernière session ?*

Le profil est enregistré toutes les quelques secondes tant qu'il change, et quand le jeu se
déconnecte ou que l'application se ferme. Une capture ouverte depuis le disque affiche le profil de
son jeu sans le modifier.

## État persistant (§38)

Ce qui a été changé dans un jeu — shaders désactivés, shaders remplacés, surlignage magenta — est
perdu quand le jeu se ferme : c'est l'add-on qui le porte, et les numéros de shaders ne veulent plus
rien dire au lancement suivant. **Enregistrer les changements de la session** l'écrit dans
`Games/<exécutable>/state.cygimod/`, un paquet de mod indexé par hash sémantique, le format que lit
CyGPUInjector. Les changements de la session remplacent ceux déjà enregistrés pour les mêmes
shaders ; les changements enregistrés pour des shaders que cette session n'a pas chargés sont gardés.

**Réappliquer maintenant**, ou **Réappliquer à la connexion de ce jeu**, les applique à nouveau :
chaque changement est envoyé quand le jeu a chargé son shader, ce qui peut arriver plusieurs minutes
après le début de la session. Un remplacement enregistré est vérifié par rapport au shader que le jeu
a maintenant, avec le même contrôle qu'un nouveau remplacement, et il n'est pas envoyé si une mise à
jour du jeu a modifié ce shader. Les changements réappliqués sont enregistrés comme les autres : ils
peuvent être exportés et enregistrés de nouveau.

## Accès depuis l'interface

Toujours asynchrone ou borné : désassemblage et décompilation en arrière-plan, chargement des notes à
la demande, listes virtualisées. Des milliers de shaders et des dizaines de milliers de draw calls
sont le cas normal (§61), pas le cas limite.
