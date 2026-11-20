# CyGPUInspector 0.5.1

[English](VERSION.md) · [README](README.fr.md) · [Version publiée](https://github.com/MrMybal/CyGPUInspector/releases/tag/v0.5.1)

**Statut : Alpha — expérimental.** Disponible pour les essais et les retours ; les fonctionnalités
et la compatibilité peuvent être incomplètes. Ce statut concerne l’application, pas la maturité de ses dépendances.

## Build publié

- Version **0.5.1**, publiée le **20 novembre 2026**.
- **Windows x64**, archive ZIP portable : [télécharger](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.1/CyGPUInspector-0.5.1-win64.zip).
- [Fichier SHA-256](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.1/CyGPUInspector-0.5.1-win64.zip.sha256).

## Changements de la 0.5.1

- CyMCPSDK **0.7.2**, révision `e562c731a146d599e3404718ea8414bb5b96b66d`, inclut les profils
  d'accès local introduits dans `34d4bca` et est compilé depuis les sources locales figées.
- Le MCP HTTP local est activé par défaut à `http://127.0.0.1:41823/mcp`. Le panneau MCP permet
  de désactiver l'endpoint, copier son adresse effective et sélectionner un profil d'accès.
- **Free** est le défaut : les **111 opérations enregistrées** sont autorisées sans jeton ni
  confirmation supplémentaire. Les schémas d'arguments et les prérequis métier restent appliqués.
- **Local contrôlé**, **Jeton seul** et **Protégé** restent disponibles. Les profils contrôlés
  appliquent le niveau de permission de l'hôte ; les profils avec jeton exigent le jeton de session
  en mémoire, à copier ou renouveler dans le panneau. Protégé exige jeton et permission métier.
- Sans choix explicite, la configuration migre vers Free ; profils choisis et préférences sans
  rapport sont conservés. Les anciens niveaux de permission explicites restent en accès contrôlé.
- Stdio/CLI et modes du tchat intégré sont conservés. Les appels HTTP passent par le registre
  partagé et le thread d'interface. Le test d'annulation du tchat attend désormais que son
  fournisseur local ait reçu la requête avant de l'annuler.
- Manuel anglais/français actualisé avec **49 sujets par langue**, recherche, images, schémas et
  références API. [Télécharger le lecteur HTML complet hors ligne](https://github.com/MrMybal/CyGPUInspector/releases/download/v0.5.1/CyGPUInspector-Documentation-0.5.1.zip).

CyUI **1.2.0** (`ff023e6`) et CyDoc **0.1.0** (`6ddf9a5`) sont inchangés.
L'application native, le compagnon et les add-ons sont recompilés. Les binaires existants du
wrapper Unreal sont réutilisés ; cette publication ne revendique pas de nouvelle compilation Unreal.
GitHub Pages reste au déploiement précédent : son actualisation exige un workflow manuel explicite.
Les manuels embarqué et HTML téléchargeable sont à jour. Les fichiers déjà publiés restent inchangés.

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
