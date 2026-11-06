---
id: graph
title: Graphe de frame et inspection des commandes
description: Suivre passes, ressources et commandes pour examiner un effet de rendu.
section: Guide utilisateur
order: 5
parent: capture
sourceRevision: 6d1626113101
---

# Graphe de frame et inspection des commandes {#frame-graph-and-command-inspection}

## Retrouver un effet de rendu {#follow-a-rendering-effect}
Ouvrez Graphe de frame et examinez les passes dérivées. Partez d’une ressource identifiable, comme la couleur de la scène ou la profondeur. Suivez ses lecteurs/rédacteurs, les changements de résolution, calculs, copies et compositions. Sélectionnez la passe correspondante puis examinez ses commandes dans Événements de la frame.

Le graphe est reconstruit à partir des usages observés ; ce n’est pas le render graph du moteur. Les lectures runtime peuvent être incomplètes, notamment pour le calcul. Une capture profonde apporte les descripteurs nécessaires pour étayer une dépendance. L’API ReShade ne transmet pas à l’observateur les marqueurs du jeu donnant les noms de passes.

## Vérifier un nom {#verify-a-name}
Un nom Bloom, Lighting ou Tonemap déduit est une piste, pas une preuve. Vérifiez les liaisons des shaders, formats de ressources, ordre des passes et image résultante. Les noms utilisateur et suggestions IA restent distincts des classifications dérivées. La confiance qualifie l’inférence, pas l’exhaustivité de la capture.

## Examiner une commande {#inspect-a-command}
Dans Événements de la frame, choisissez un dessin, dispatch, copie, effacement ou autre commande. Les filtres shader/passe réduisent la liste. Détails présente le pipeline, la ressource et l’événement liés. La capture profonde ajoute liaisons, barrières et état fixe du pipeline. Un champ inconnu est un état non capturé, pas nécessairement désactivé.

L’inspecteur de pipeline est filtrable et en lecture seule. Les entiers gardent leur valeur exacte. La réflexion d’un shader décrit ses déclarations ; les liaisons profondes décrivent l’usage observé pour une commande. Comparez les deux pour déterminer l’image associée à un registre.

Pour les modèles et règles de déduction, voir [Graphe de frame](doc:ref-framegraph), [Suivi des ressources](doc:ref-resourcetracking) et [modes de capture](doc:capture).


## Illustrations {#illustrations}

### Inspection d’une frame {#figure-0}

Rendu réel de l’application avec les données synthétiques de démonstration. Sélectionnez une passe, un shader ou un événement pour relier les vues.

```image
{"id":"figure-0","src":"assets/media/overview-fr.png","alt":"Inspection d’une frame","controls":"minimal"}
```

### Circulation des données {#figure-1}

ReShade charge l’observateur. Le standalone possède l’analyse, les fichiers et les décisions d’autorisation.

```diagram
{"type":"flow","title":"Circulation des données","nodes":[{"id":"game","label":"Jeu + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Mémoire partagée / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Analyse standalone","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profils / mods","x":600,"y":160,"width":280},{"id":"ai","label":"Fournisseur IA / agent MCP","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```
