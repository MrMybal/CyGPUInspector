# Commencer ici

CyGPUInspector inspecte les frames GPU en direct ou sauvegardées, les shaders, ressources et mesures de temps. ReShade est l’agent de capture dans un jeu compatible ; le standalone effectue l’analyse à l’extérieur. L’application Windows x64 couvre les parcours Direct3D 11/12 implémentés. Vulkan et OpenGL ne sont pas encore pris en charge par ce produit.

## Votre première analyse
1. Lancez [la démonstration sans jeu](help:installation). Elle ne nécessite aucun compte de fournisseur et ne modifie aucun jeu.
2. Découvrez [l’espace de travail](help:navigation), puis cliquez sur une passe mesurée dans la timeline.
3. Retrouvez les shaders, événements et ressources de cette passe.
4. Sauvegardez une frame pour reprendre l’analyse plus tard.

Pour un vrai jeu, installez ReShade avec prise en charge des add-ons, chargez l’add-on de capture et connectez le standalone. Le [schéma des échanges](figure:1) précise le rôle des composants.

## Utiliser ce guide
F1 ouvre l’aide de la vue active ; Aide > Guide utilisateur ouvre cet accueil. Le logiciel lance le lecteur CyDoc fourni par une URL de fichier locale dans votre navigateur, sans serveur ni requête réseau. L’anglais est la langue par défaut ; sélectionnez Français dans le lecteur sans modifier les préférences de l’application.

Recherchez dans les titres et textes, utilisez le sommaire, les sections et les liens vers les illustrations. Positions de lecture, favoris et notes utilisent les outils locaux CyDoc ; leur persistance dépend du stockage autorisé par le navigateur sur une origine fichier. Exportez vos notes pour les sauvegarder. Les images proposent zoom et annotations avec des contrôles épurés. Agrandir reste dans la fenêtre de documentation ; Échap réduit la vue. Les commandes tactiles restent accessibles.

## Interpréter les données
Les captures sont des rendus réels de l’application avec des données synthétiques. Les noms de passes peuvent être dérivés ou donnés par l’utilisateur, le HLSL est reconstruit et les mesures ont leurs limites. Aucune capture ne rejoue la frame. Consultez [les limites et le dépannage](help:troubleshooting) avant de conclure.

Le Guide utilisateur décrit les manipulations ; Référence technique contient les documents complets d’architecture, de protocole et de fonctions. L’aide est locale. Les liens Web ne sont suivis que par un clic volontaire et peuvent nécessiter un accès Internet.
