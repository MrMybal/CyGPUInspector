# CyGPUInspector 0.5.0

[English](VERSION.md) · [README](README.fr.md) · [Version publiée](https://github.com/MrMybal/CyGPUInspector/releases/tag/v0.5.0)

**Statut : Alpha — expérimental.** Disponible pour les essais et les retours ; les fonctionnalités
et la compatibilité peuvent être incomplètes. Ce statut concerne l’application, pas la maturité de ses dépendances.

## Build publié

- Version **0.5.0**, publiée le **19 novembre 2026**.
- **Windows x64**, archive ZIP portable : [télécharger](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.0/CyGPUInspector-0.5.0-win64.zip).
- [Fichier SHA-256](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.0/CyGPUInspector-0.5.0-win64.zip.sha256).

## Changements de la 0.5.0

- CyMCPSDK **0.7.0**, révision `791d4d7`, fournit les sessions Codex natives, les conversations HTTP
  en streaming, l'intégration Claude bornée, l'annulation et les checkpoints privés optionnels.
- **111 opérations partagées** (34 outils GPU et 77 opérations applicatives) couvrent connexion,
  captures, filtres, dispositions, aperçus, annotations, HLSL, mods et assistant depuis tchat, CLI et MCP.
- Exports gérés et sauvegardes préservent notes, brouillons, dispositions et profils récupérables.
- Manuel anglais/français actualisé : **49 sujets par langue**, images, schémas, recherche et références API.
  F1 et l'aide contextuelle ouvrent le lecteur local complet sans serveur ni Internet.
- Le [manuel HTML complet hors ligne](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.0/CyGPUInspector-Documentation-0.5.0.zip)
  est aussi proposé séparément, avec scripts, styles et ressources.

CyUI **1.2.0** (`ff023e6`) et CyDoc **0.1.0** (`6ddf9a5`) restent inchangés.
L'application, le proxy et l'add-on natif de capture sont recompilés. Les binaires existants du
wrapper Unreal sont réutilisés ; cette publication ne revendique pas de nouvelle compilation Unreal.
Le déploiement GitHub Pages est manuel : le manuel hébergé reste au déploiement précédent jusqu'à
une publication explicite. Le manuel embarqué et l'export HTML téléchargeable sont à jour.
Les fichiers de la version 0.4.1 déjà publiée restent inchangés.

## Limitations connues

- Windows x64 et procédures D3D11/12 prises en charge ; Vulkan et OpenGL ne le sont pas.
- Aucun rejeu de frame, historique de pixel, débogage pas à pas des shaders ou récupération du HLSL original.
- Les captures ordinaires enregistrées contiennent les métadonnées, pas les pixels GPU en direct ;
  l’export profond des buffers est distinct et peut échouer pour certaines ressources.
- Les passes reconstruites, le code des shaders et les interprétations IA ont leurs limites ; les
  timings dépendent des événements disponibles, de la capacité des requêtes et du niveau de capture.
- Le jeu doit accepter ReShade. Aucun contournement d’anti-triche ou d’autres protections n’est fourni.

Consultez [le dépannage et les limitations](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/fr/troubleshooting)
et [la référence complète des limites](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/fr/ref-limitations).

## Bugs et retours

Consultez
le [suivi des problèmes](https://github.com/MrMybal/CyGPUInspector/issues) pour les signalements
actuels et les problèmes reproductibles. Les limitations ci-dessus ne sont pas des tickets de bugs confirmés.

## Mise à jour

Extrayez tout le paquet et préservez préférences, disposition, base des shaders, profils de jeux
et captures, ainsi que les sauvegardes Automation. L’historique optionnel reste dans Local AppData. Gardez le manuel et les DLL près de l’exécutable, ainsi que toutes les notices de licence.
Consultez le [guide d’installation](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/fr/installation).
