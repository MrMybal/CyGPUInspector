---
id: api-overview
title: API publique d’intégration
description: Appeler les exports de capture et lire ou écrire des paquets de mods portables.
section: Référence développeur
order: 100
sourceRevision: 0bc94ada6381
---

# API publique d’intégration {#public-api}

Utilisez l’API C locale si votre hôte tourne dans le processus du jeu, ou l’API C++ des paquets pour lire et écrire des mods portables. Les agents externes utilisent [MCP](doc:mcp) ; ils ne chargent pas ces exports.

## Capture dans le processus {#in-process}

Incluez `CyGPUInspectorCore/InProcessApi.h`. ReShade doit déjà avoir chargé `CyGPUInspectorRS.addon64` dans le même processus. Résolvez les exports nommés avec l’API de modules de la plateforme ; ne chargez pas une autre copie pour obtenir un pointeur de fonction. Vérifiez la version de l’API avant le périmètre de viewport de version 2.

Toutes les structures commencent par `size`. Initialisez-le avec `sizeof` votre structure. Les options sélectionnent 1–8 frames, les descripteurs, barrières, timings par draw et exports de buffers. L’état contient `api_version`, `device_count`, `standalone_connected`, `capture_stage`, les compteurs de frames et `pending_request`.

```cpp
// requestCapture est un PFN_CyGPUInspectorRS_RequestCapture déjà résolu.
CygiCaptureOptions options{};
options.size = sizeof(options);
options.frame_count = 1;
options.include_bindings = 1;
if (requestCapture(&options) == 0) {
    // Aucun device ou options invalides : examiner l’état avant de réessayer.
}
```

Une requête acceptée est en attente, pas terminée. Suivez l’état et attendez la fin de l’export des buffers par le standalone avant de copier les fichiers. Le [workflow Unreal](doc:unreal) décrit l’intégration de l’hôte pris en charge.

## Paquets de mods portables {#mod-packages}

Incluez `CyGPUInspectorCore/ModPackage.hpp` et liez `CyGPUInspectorCore`. `cygi::ModPackage` contient les métadonnées lisibles et les actions `ModEntry`. Un remplacement contient le bytecode et sa provenance source éventuelle ; une désactivation identifie le shader par hash sémantique. `Save` écrit un dossier et `Load` le valide. Aucun des deux n’applique le paquet au jeu en cours.

```cpp
cygi::ModPackage package;
std::string error;
if (!cygi::ModPackage::Load("Example.cygimod", package, error, false)) {
    // Afficher error ; ne pas utiliser un paquet partiellement lu.
}
```

[Export de mod](doc:mods) décrit le workflow utilisateur. Les références de fonctions sous cette rubrique détaillent paramètres, résultats et appels associés.
