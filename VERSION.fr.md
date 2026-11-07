# CyGPUInspector 0.4.1

[English](VERSION.md) · [README](README.fr.md) · [Version publiée](https://github.com/MrMybal/CyGPUInspector/releases/tag/v0.4.1)

**Statut : Alpha — expérimental.** Disponible pour les essais et les retours ; les fonctionnalités
et la compatibilité peuvent être incomplètes. Ce statut concerne l’application, pas la maturité de ses dépendances.

## Build publié

- Version **0.4.1**, publiée le **6 novembre 2026**.
- **Windows x64**, archive ZIP portable : [télécharger](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.4.1/CyGPUInspector-0.4.1-win64.zip).
- [Fichier SHA-256](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.4.1/CyGPUInspector-0.4.1-win64.zip.sha256).

## Changements de la 0.4.1

- Manuel anglais/français complet avec 48 sujets, captures, schémas et recherche.
- Lecteur CyDoc actualisé : recherche dans l’en-tête, navigation hiérarchique, infobulles rapides
  et barre latérale élargie.
- Références des API publiques C/C++ avec signatures, paramètres et exemples.
- F1 et l’aide contextuelle ouvrent le lecteur embarqué `Documentation/Documentation.html`,
  sans serveur ni connexion Internet.
- Les paquets portables comprennent le manuel complet et un `DocAI.md` séparé.

Le lecteur utilise CyDoc **0.1.0**, révision `6ddf9a5` ; l’interface native utilise CyUI **1.2.0**,
révision `ff023e6`. Il s’agit de versions de composants, pas de versions supplémentaires de l’application.
Les binaires existants du plugin Unreal sont réutilisés ; cette publication ne correspond pas à
une nouvelle compilation du module Unreal.

La présentation des README du dépôt et le statut Alpha ont été actualisés après la publication
du paquet 0.4.1. Ils n’impliquent ni remplacement du ZIP ni fonctionnalité supplémentaire dans cette version.

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

Les notes publiées de la 0.4.1 ne donnent pas de liste détaillée de corrections de bugs. Consultez
le [suivi des problèmes](https://github.com/MrMybal/CyGPUInspector/issues) pour les signalements
actuels et les problèmes reproductibles. Les limitations ci-dessus ne sont pas des tickets de bugs confirmés.

## Mise à jour

Extrayez tout le paquet et préservez préférences, disposition, base des shaders, profils de jeux
et captures. Gardez le manuel et les DLL près de l’exécutable, ainsi que toutes les notices de licence.
Consultez le [guide d’installation](https://mrmybal.github.io/CyGPUInspector/Documentation.html#/fr/installation).
