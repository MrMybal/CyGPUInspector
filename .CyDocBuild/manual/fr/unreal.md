---
id: unreal
title: Capturer depuis Unreal Engine
description: Installer le plugin Unreal et capturer un viewport éditeur ou de développement.
section: Guide utilisateur
order: 14
parent: installation
sourceRevision: d455c6459e50
---

# Capturer depuis Unreal Engine {#capture-from-unreal-engine}

## Installer le bon plugin {#install-the-matching-plugin}
Choisissez Unreal/UE5.3 ou Unreal/UE5.8 selon le moteur utilisé. Copiez CyGPUInspector dans Plugins du projet. Un projet Blueprint seul nécessite le plugin précompilé pour ce moteur ; un projet C++ peut compiler une source compatible. L’intégration source vise Unreal 5.3+, mais un binaire d’une autre version n’est pas interchangeable.

Conservez les binaires ReShade/add-on de ThirdParty et les licences du plugin. Dans Paramètres du projet > Plugins > CyGPUInspector, vérifiez leurs chemins et celui du standalone. Redémarrez l’éditeur : le loader intervient avant la création du périphérique graphique. Consultez LogCyGPUInspectorLoader si le chargement échoue.

## Capturer {#capture}
Utilisez le bouton de barre d’outils, la commande console ou l’interface Blueprint décrite dans [la référence Unreal](doc:ref-unrealplugin). Gardez le viewport cible visible et actif. L’intégration marque son rendu pour limiter les commandes profondes et choisir l’image finale ; la timeline runtime du standalone peut décrire une frame plus large.

Un viewport inactif peut ne redessiner qu’après un changement. Le plugin demande un rafraîchissement et peut expirer en attendant une frame adéquate. Contrôlez la portée de la capture et les noms de ressources plutôt que de supposer que toutes les fenêtres sont incluses.

## Configurations de build {#build-configurations}
Le loader vise Editor, Development et DebugGame. Shipping désactive le chargement/capture actif ; les nœuds Blueprint peuvent rester sans activer l’inspection. Sur les autres plateformes ils sont inactifs. Les dépendances runtime sont déclarées pour les configurations compatibles.

Load In Game dans les paramètres contrôle le chargement normal en jeu. -NoCyGPUInspector le désactive pour un lancement ; -CyGPUInspector force le parcours prévu. Logs/réglages sont placés sous Saved/CyGPUInspector du projet, pas dans l’installation commune du moteur.

Ne changez pas le descripteur pour rebaptiser un ancien binaire. Recompilez pour le moteur réel ou utilisez le paquet fourni correspondant.


## Illustrations {#illustrations}

### Circulation des données {#figure-0}

ReShade charge l’observateur. Le standalone possède l’analyse, les fichiers et les décisions d’autorisation.

```diagram
{"type":"flow","title":"Circulation des données","nodes":[{"id":"game","label":"Jeu + ReShade","x":0,"y":0,"width":280},{"id":"transport","label":"Mémoire partagée / pipes","x":300,"y":0,"width":280},{"id":"app","label":"Analyse standalone","x":600,"y":0,"width":280},{"id":"files","label":"Captures / profils / mods","x":600,"y":160,"width":280},{"id":"ai","label":"Fournisseur IA / agent MCP","x":300,"y":160,"width":280}],"edges":[{"from":"game","to":"transport"},{"from":"transport","to":"app"},{"from":"app","to":"files"},{"from":"ai","to":"app"}]}
```

### Choisir la bonne capture {#figure-1}

Observation en direct, sauvegarde d’une frame et export profond des buffers répondent à des besoins différents ; aucun ne rejoue la frame.

```diagram
{"type":"flow","title":"Choisir la bonne capture","nodes":[{"id":"live","label":"Historique en direct","x":0,"y":0,"width":280},{"id":"saved","label":"Une frame sauvegardée","x":300,"y":0,"width":280},{"id":"deep","label":"État profond des commandes","x":600,"y":0,"width":280},{"id":"bounded","label":"Observations bornées","x":0,"y":160,"width":280},{"id":"offline","label":"Hors ligne, sans pixels vivants","x":300,"y":160,"width":280},{"id":"export","label":"Export PNG / DDS séparé","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```
