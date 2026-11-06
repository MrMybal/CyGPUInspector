---
id: troubleshooting
title: Dépannage et limites connues
description: Résoudre les problèmes de connexion, timing, aperçu, compilation et aide.
section: Guide utilisateur
order: 15
parent: welcome
sourceRevision: ba09078d40d4
---

# Dépannage et limites connues {#troubleshooting-and-known-limits}

## Aucune session {#no-session}
Vérifiez processus 64 bits, parcours D3D11/12 compatible, ReShade avec add-ons complets et chargement réel de CyGPUInspectorRS. Consultez ReShade.log et actualisez Connexions. Le standalone seul ne produit pas de capture. La démonstration sans jeu sépare un souci d’installation d’un souci de l’application.

## Temps absents ou événements perdus {#no-timings-or-missing-events}
Choisissez Pass Timing ou un niveau/ciblage adapté, puis attendez les résultats différés. Regardez pertes et capacité des timestamps dans État. Une fréquence nulle/inconnue ou une mesure manquante ne doit pas produire un faux temps. Le tampon d’événements est borné et perd des données plutôt que de bloquer le jeu. Un graphe issu d’événements partiels peut être incomplet.

## Aperçu noir ou indisponible {#black-unavailable-preview}
Vérifiez le direct, l’existence de la ressource, l’adaptateur commun et la compatibilité format/état. Essayez une plage/canal adapté et les réglages de profondeur/reverse-Z. Une frame hors ligne ordinaire n’a pas de pixels. L’export profond peut échouer pour un buffer sans invalider tous les autres fichiers.

## Compilation ou remplacement refusé {#compilation-or-replacement-refused}
Vérifiez point d’entrée, étage/profil, signatures de ressources et compilateurs dans Outils. Conservez la paire DXC fournie pour SM6/signature DXIL. Lisez diagnostic et différences de compatibilité. Compiler ne prouve pas le même comportement. Restaurer annule un remplacement appliqué ; Annuler dans l’éditeur ne change que le texte source.

## IA / MCP indisponible {#ai-mcp-unavailable}
Vérifiez connexion CLI ou URL/modèle API et démarrage du serveur local. Les clés API durent une session. Pour MCP, démarrez le standalone, libérez la connexion de proxy unique et lisez niveau/refus côté hôte. Une analyse en attente demande une requête ultérieure, pas des appels concurrents répétés.

## Aide absente ou partiellement traduite {#help-missing-or-partly-translated}
Restaurez le dossier Documentation généré complet à côté de l’EXE et rouvrez Documentation/Documentation.html ; le dossier de travail ne doit pas compter. Le choix de langue est indépendant de l’application. Les éditions anglaise et française du manuel distribué sont complètes. Une image absente donne un message de récupération sans empêcher la lecture.

## Limites fondamentales {#fundamental-limits}
Pas de replay, historique de pixel, exécution pas à pas de shader ni récupération du HLSL original. Les noms de passes sont déduits/donnés ; descripteurs et temps multi-listes ont des limites explicites. Historiques et tailles de captures sont bornés volontairement. Consultez [la référence des limites](doc:ref-limitations) avant de conclure à un dysfonctionnement.


## Illustrations {#illustrations}

### Choisir la bonne capture {#figure-0}

Observation en direct, sauvegarde d’une frame et export profond des buffers répondent à des besoins différents ; aucun ne rejoue la frame.

```diagram
{"type":"flow","title":"Choisir la bonne capture","nodes":[{"id":"live","label":"Historique en direct","x":0,"y":0,"width":280},{"id":"saved","label":"Une frame sauvegardée","x":300,"y":0,"width":280},{"id":"deep","label":"État profond des commandes","x":600,"y":0,"width":280},{"id":"bounded","label":"Observations bornées","x":0,"y":160,"width":280},{"id":"offline","label":"Hors ligne, sans pixels vivants","x":300,"y":160,"width":280},{"id":"export","label":"Export PNG / DDS séparé","x":600,"y":160,"width":280}],"edges":[{"from":"live","to":"bounded"},{"from":"saved","to":"offline"},{"from":"deep","to":"export"}]}
```

### Deux contrôles d’autorisation indépendants {#figure-1}

Les modes de l’assistant intégré et les niveaux d’outils MCP externes sont des réglages distincts. Le standalone contrôle les deux parcours.

```diagram
{"type":"flow","title":"Deux contrôles d’autorisation indépendants","nodes":[{"id":"assistant","label":"Assistant : Auto / Demander / Tout","x":0,"y":0,"width":280},{"id":"host","label":"Contrôles dans le standalone","x":320,"y":0,"width":280},{"id":"mcp","label":"MCP : Lire / Déboguer / Modifier","x":640,"y":0,"width":280},{"id":"provider","label":"Fournisseur CLI / API","x":0,"y":160,"width":280},{"id":"actions","label":"Actions jeu / fichiers autorisées","x":320,"y":160,"width":280}],"edges":[{"from":"assistant","to":"host"},{"from":"mcp","to":"host"},{"from":"assistant","to":"provider"},{"from":"host","to":"actions"}]}
```
