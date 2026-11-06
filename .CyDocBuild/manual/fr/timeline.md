---
id: timeline
title: Lire les temps GPU et la timeline continue
description: Lire les intervalles GPU, suivre plusieurs frames et interpréter les mesures manquantes.
section: Guide utilisateur
order: 4
parent: capture
sourceRevision: 20bd508ab5d3
---

# Lire les temps GPU et la timeline continue {#read-gpu-timings-and-the-continuous-timeline}

## Examiner une frame {#inspect-one-frame}
Choisissez Timeline de frame > Une frame au niveau Pass Timing ou supérieur. L’historique montre les temps CPU récents et les mesures GPU disponibles. Les lignes présentent la frame, les passes et les commandes disposant de mesures exploitables. Cliquez sur une barre puis examinez sa durée, ses shaders, ressources et commandes.

Une passe atténuée ou une valeur précédée de ~ est estimée. Un timestamp absent ne signifie pas un coût GPU nul. Les couleurs décrivent les rôles ; les largeurs suivent l’intervalle mesuré/estimé et l’unité de la règle. Les noms utilisateur, dérivés et hypothèses IA gardent leur provenance.

## Naviguer {#navigate}
La molette zoome autour du pointeur. Le glisser déplace la vue ; le canevas commun accepte aussi le bouton central et la navigation horizontale. Double-clic/Ajuster restaure la plage utile. Le curseur de la vue continue déplace la portion d’historique conservée. Un glisser ne doit pas sélectionner une commande sans rapport.

## Suivre plusieurs frames {#follow-several-frames}
Choisissez Continue pour placer les frames mesurées successives sur l’horloge GPU. Partez d’un pic de l’historique pour retrouver les passes de la frame. Le tableau détaille le numéro de la frame choisie, les débuts et les durées. Un intervalle peut correspondre à une attente GPU ou à du travail hors de la zone mesurée ; les frames manquantes sont distinguées. L’historique est borné : les commandes anciennes peuvent avoir disparu même si une barre compacte reste visible.

## Cibler une mesure {#use-timing-focus}
Les actions de mesure précise d’un shader/dessin/ressource demandent des mesures ciblées tout en conservant le suivi par passe. Contrôlez l’état du ciblage et attendez les résultats différés. Le nombre de requêtes est limité ; les marques perdues sont indiquées. Chaque résultat garde son propre numéro de frame.

## Interpréter les intervalles {#interpret-intervals}
ReShade notifie avant une commande : son temps est l’intervalle jusqu’à la mesure suivante, pas une durée isolée matériellement. En D3D12, l’ordre d’enregistrement de plusieurs listes n’est pas l’ordre d’exécution GPU. La charge de la machine et l’instrumentation influencent les chiffres ; comparez des conditions répétables. Voir [la référence du profilage](doc:ref-gpuprofiling).


## Illustrations {#illustrations}

### Timeline GPU continue {#figure-0}

Les frames et passes mesurées partagent une horloge GPU. Un intervalle d’attente ne prouve pas une cause particulière.

```image
{"id":"figure-0","src":"assets/media/timeline-fr.png","alt":"Timeline GPU continue","controls":"minimal"}
```

### Inspection d’une frame {#figure-1}

Rendu réel de l’application avec les données synthétiques de démonstration. Sélectionnez une passe, un shader ou un événement pour relier les vues.

```image
{"id":"figure-1","src":"assets/media/overview-fr.png","alt":"Inspection d’une frame","controls":"minimal"}
```
