---
id: capture
title: Niveaux de suivi et modes de capture
description: Choisir observation en direct, frame sauvegardée ou capture profonde bornée.
section: Guide utilisateur
order: 3
sourceRevision: 7d3d8efcddd1
---

# Niveaux de suivi et modes de capture {#tracking-levels-and-the-capture-modes}

## Commencer par l’observation continue {#start-with-runtime-observation}
Connectez une session et choisissez le niveau le moins coûteux qui répond à votre question. Les inventaires et le suivi des commandes apportent progressivement plus de données. Pass Timing ajoute les temps par passe ; Draw Timing coûte davantage. Sans timestamps, les barres peuvent représenter l’ordre ou le nombre de commandes : lisez la règle plutôt que de convertir toute largeur en millisecondes.

## Choisir la sortie {#choose-your-output}
| Besoin | Action | Données conservées |
| --- | --- | --- |
| Suivre les performances récentes | Runtime / Timeline de frame | Historique borné en direct |
| Reprendre l’analyse plus tard | Sauvegarder la frame | Une frame hors ligne et le bytecode de ses shaders utilisés |
| Examiner les liaisons et buffers | Capture profonde | État borné des commandes et exports PNG/DDS séparés |
| Préparer un cas pour un analyste | Snapshot d’analyse IA | Frame sauvegardée et analyses disponibles |

## Capturer une frame en profondeur {#capture-a-deep-frame}
1. Connectez le bon producteur en direct et ouvrez Capture profonde.
2. Armez la capture. L’enregistrement débute à une frontière de frame et s’arrête après l’intervalle borné demandé.
3. Attendez la fin ; contrôlez la plage, les compteurs de commandes/descripteurs et les indications de données incomplètes ou perdues.
4. Sélectionnez une commande pour examiner liaisons, état du pipeline et barrières.
5. Attendez la fin de l’export des buffers avant de copier le dossier. La liste distingue les fichiers non copiés et les échecs.

L’image finale et les buffers sont exportés en PNG consultable et en DDS conservant les données lorsque c’est possible ; capture.json les décrit. Une simple frame sauvegardée hors ligne ne contient pas ces pixels. Le [schéma des captures](doc:capture#figure-0) distingue les parcours.

## Ce que la capture ne fait pas {#what-it-cannot-do}
Aucun mode ne rejoue les commandes, ne fournit l’historique d’un pixel, ne débogue un shader pas à pas ni ne récupère le HLSL original. La capture profonde borne le nombre de commandes/descripteurs et peut ignorer un état fixé avant son armement. Une capture partielle l’indique ; inconnu ne signifie pas vide. Voir [les bornes et mécanismes détaillés](doc:ref-capturemodes) et [les limites](doc:ref-limitations).


## Illustrations {#illustrations}

### Choisir la bonne capture {#figure-0}

Observation en direct, sauvegarde d’une frame et export profond des buffers répondent à des besoins différents ; aucun ne rejoue la frame.

```diagram
{"type":"flow","title":"Choisir la bonne capture","nodes":[{"id":"live","label":"Historique en direct","x":0,"y":0,"width":280},{"id":"saved","label":"Une frame sauvegardée","x":300,"y":0,"width":280},{"id":"deep","label":"État profond des commandes","x":600,"y":0,"width":280},{"id":"bounded","label":"Observations bornées","x":0,"y":160,"width":280},{"id":"offline","label":"Hors ligne, sans pixels vivants","x":300,"y":160,"width":280},{"id":"export","label":"Export PNG / DDS séparé","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```

### Inspection d’une frame {#figure-1}

Rendu réel de l’application avec les données synthétiques de démonstration. Sélectionnez une passe, un shader ou un événement pour relier les vues.

```image
{"id":"figure-1","src":"assets/media/overview-fr.png","alt":"Inspection d’une frame","controls":"minimal"}
```
