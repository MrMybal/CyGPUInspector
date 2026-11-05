# Graphe de frame et inspection des commandes

## Retrouver un effet de rendu
Ouvrez Graphe de frame et examinez les passes dérivées. Partez d’une ressource identifiable, comme la couleur de la scène ou la profondeur. Suivez ses lecteurs/rédacteurs, les changements de résolution, calculs, copies et compositions. Sélectionnez la passe correspondante puis examinez ses commandes dans Événements de la frame.

Le graphe est reconstruit à partir des usages observés ; ce n’est pas le render graph du moteur. Les lectures runtime peuvent être incomplètes, notamment pour le calcul. Une capture profonde apporte les descripteurs nécessaires pour étayer une dépendance. L’API ReShade ne transmet pas à l’observateur les marqueurs du jeu donnant les noms de passes.

## Vérifier un nom
Un nom Bloom, Lighting ou Tonemap déduit est une piste, pas une preuve. Vérifiez les liaisons des shaders, formats de ressources, ordre des passes et image résultante. Les noms utilisateur et suggestions IA restent distincts des classifications dérivées. La confiance qualifie l’inférence, pas l’exhaustivité de la capture.

## Examiner une commande
Dans Événements de la frame, choisissez un dessin, dispatch, copie, effacement ou autre commande. Les filtres shader/passe réduisent la liste. Détails présente le pipeline, la ressource et l’événement liés. La capture profonde ajoute liaisons, barrières et état fixe du pipeline. Un champ inconnu est un état non capturé, pas nécessairement désactivé.

L’inspecteur de pipeline est filtrable et en lecture seule. Les entiers gardent leur valeur exacte. La réflexion d’un shader décrit ses déclarations ; les liaisons profondes décrivent l’usage observé pour une commande. Comparez les deux pour déterminer l’image associée à un registre.

Pour les modèles et règles de déduction, voir [Graphe de frame](help:ref-FrameGraph), [Suivi des ressources](help:ref-ResourceTracking) et [modes de capture](help:capture).
