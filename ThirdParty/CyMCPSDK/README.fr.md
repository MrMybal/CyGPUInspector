<table width="100%">
  <tr>
    <td width="300" align="left" valign="top">
      <a href="Documentation/Documentation.html#/fr/overview">Manuel</a><br>
      <a href="https://github.com/MrMybal/CyMCPSDK">Dépôt</a><br><br>
      <sub>README</sub><br>
      <a href="README.md">English</a> · <a href="README.fr.md">Français</a>
    </td>
    <td width="400" align="center" valign="middle">
      <strong>CyMCPSDK</strong><br><sub>SDK C++20</sub>
    </td>
    <td width="300" align="right" valign="top">
      <p><sub>Licence de la bibliothèque</sub><br><a href="LICENSE"><strong>MIT</strong></a></p>
      <p><sub>Version</sub><br><a href="VERSION.fr.md"><strong>0.7.0</strong></a><br><strong>EN DÉVELOPPEMENT</strong></p>
      <p><a href="https://github.com/MrMybal/CyMCPSDK/releases/tag/v0.5.6"><sub>Télécharger Chat Lab · Windows x64</sub></a></p>
    </td>
  </tr>
</table>

<h1 align="center">CyMCPSDK</h1>

Une bibliothèque C++20 modulaire pour les développeurs qui veulent permettre aux agents IA
d'utiliser leur app, intégrer des conversations et accéder aux outils d'autres applications.
Elle fournit des transports SDK/MCP natifs et un panneau de tchat Markdown CyUI optionnel.

> **EN DÉVELOPPEMENT · 0.7.0**
>
> L'exemple de tchat Windows est disponible en ZIP portable. La distribution Linux/macOS et les
> installations de dépendances restent en cours de réalisation. Consulter les [notes de version et problèmes connus](VERSION.fr.md).

[![Catalogue et paramètres des fournisseurs dans Chat Lab](assets/screenshots/providers-fr.png)](assets/screenshots/providers-fr.png)

## Ce que vous pouvez créer

Enregistrer une fois les opérations de l'application, puis les exposer par appels natifs,
SDK JSON ou serveur MCP local authentifié. Connecter un fournisseur IA à ces opérations,
ou utiliser le client MCP pour transférer des outils choisis depuis une autre application.

Les conversations utilisent des agents CLI, API natives ou services de modèles locaux existants.
Le registre contient 23 profils extensibles, dont Codex, Claude Code, OpenCode, Antigravity,
les agents ACP, Responses, Messages, OpenRouter et Ollama. Les modèles et capacités réels
viennent du fournisseur configuré. Les identifiants et poids des modèles sont fournis séparément.

Chat Lab propose les messages progressifs, Markdown/code/tableaux, pièces jointes, annulation,
reconnexion exacte et historique optionnel. Son compteur de démonstration est utilisable par
une IA ou un autre client MCP/SDK. L'anglais est le défaut ; le français et cinq thèmes CyUI
sont disponibles. Le tchat s’ouvre directement. **Paramètres** donne accès aux **Fournisseurs**,
au **Journal** en tableau ou en JSONL brut et à l’**Aide**. **F1** ouvre l’aide et **Fermer**
ramène à la conversation.
Voir le [journal d’un refus de modèle simulé](assets/screenshots/logs-fr.png) et la [capture historique d’une connexion automatique à Codex](assets/screenshots/chat-auto-fr.png).
Les fournisseurs natifs proposent **Se connecter** lorsqu'une authentification est nécessaire.
Le mode manuel préremplit les paramètres conventionnels ; les API distantes affichent leur
catalogue pour un choix explicite du modèle.

## Modules

| Cible CMake | Fonction |
| --- | --- |
| `CyMCP::Core` | Opérations typées, schémas, politiques de droits, annulation et workers |
| `CyMCP::Platform` | Processus enfants avec arguments structurés et arrêt des processus possédés |
| `CyMCP::Http` | HTTP(S), streaming et serveur JSON loopback authentifié |
| `CyMCP::MCP` | Outils MCP par stdio et HTTP JSON local, serveur et client |
| `CyMCP::AI` | Factories fournisseurs, transports CLI/ACP/API, conversations et échanges d'outils |
| `CyMCP::Local` | Supervision d'un moteur local et téléchargements de dépendances vérifiés |
| `CyMCP::ChatUI` | Contrôleur asynchrone et panneau de tchat CyUI natif optionnels |
| `CyMCP::SDK` | Agrégat SDK sans dépendance graphique |

Les politiques hôtes peuvent restreindre les opérations ou autoriser explicitement toutes les
actions enregistrées. Les droits de l'agent natif restent distincts. Aucun runtime Python, CLI IA,
moteur d'inférence, modèle, runtime CUDA ou pilote n'est embarqué. Android est reporté.

Les gros catalogues utilisent la découverte à la demande en gardant toutes les commandes accessibles.
Ressources MCP, caches privés avec TTL et résultats par morceaux réduisent les échanges. Le grep
natif et ripgrep optionnel sont décrits dans le [guide optimisation](Documentation/Documentation.html#/fr/optimization).

---

## Installation

Télécharger [Chat Lab 0.5.6 pour Windows x64](https://github.com/MrMybal/CyMCPSDK/releases/download/v0.5.6/CyMCPSDK-ChatLab-0.5.6-windows-x64.zip).
La release contient l'exemple de tchat. Compiler le SDK C++ depuis les sources pour l'intégrer dans votre app.
Pour Chat Lab : Windows 10/11 x64, runtime Microsoft Visual C++ 2015–2022 x64 et DX11
(repli WARP disponible). Garder `Documentation/`, `licenses/` et les notices à côté de l'exécutable.

1. Extraire le ZIP complet dans un dossier, ou compiler l'exemple avec les commandes ci-dessous.
2. Lancer `cymcp_chat_app.exe` depuis ce dossier (`out/ChatLab-windows-x64/` après compilation).
3. Garder **Automatic connection** activé. Les CLI installés, services locaux démarrés et clés API
   d'environnement sont détectés ; la première connexion disponible est préparée sans envoyer de message.
4. Cliquer sur une autre connexion si nécessaire, puis envoyer avec **Entrée** (ou **Ctrl+Entrée**). Les vrais modèles sont proposés dans une liste.

Les paramètres manuels restent accessibles dans **Fournisseurs**. Pour essayer sans
compte IA, choisir **Demo / Markdown (no AI)** dans cet onglet puis **Connect / New chat**.

Ouvrir [Documentation/Documentation.html](Documentation/Documentation.html#/fr/quickstart)
depuis les fichiers locaux ou le paquet. Télécharger le dossier complet ; la vue HTML de GitHub
affiche les sources et n'est pas le lecteur du manuel. La lecture locale ne demande ni serveur ni Node.js.

## Sources, compilation et bibliothèques nécessaires

### Prérequis

- CMake 3.24+, compilateur C++20 et threads système.
- Windows : outils C++ Visual Studio 2022 et Windows SDK.
- Linux/macOS : API POSIX et fichiers de développement libcurl pour HTTP(S) ; voir les problèmes de plateforme actuels.
- Interface de tchat optionnelle : copie hôte de [CyUI](https://github.com/MrMybal/CyUI) avec DocumentViews.

```sh
git clone https://github.com/MrMybal/CyMCPSDK.git
cd CyMCPSDK
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
cmake --install build --config Release --prefix install
```

Pour l'exemple Windows, placer CyUI à côté du SDK ou passer son véritable emplacement :

```powershell
./tools/build-chat.ps1 -CyUISource ../CyUI
./out/ChatLab-windows-x64/cymcp_chat_app.exe --language fr
```

<details>
<summary>Lier la bibliothèque et choisir les modules optionnels</summary>

```cmake
find_package(CyMCPSDK 0.5 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE CyMCP::Core CyMCP::MCP CyMCP::AI)
```

Autre possibilité : `add_subdirectory(external/CyMCPSDK)`. Désactiver les modules inutiles
avec `CYMCP_BUILD_HTTP`, `CYMCP_BUILD_PLATFORM`, `CYMCP_BUILD_MCP`, `CYMCP_BUILD_AI` et
`CYMCP_BUILD_LOCAL`. CMake vérifie leurs dépendances. Le preset `core-only` compile le cœur des opérations.
`CYMCP_BUILD_CHAT_UI` et `CYMCP_BUILD_CHAT_APP` sont désactivés par défaut. Un consommateur
ChatUI installé fournit `CyUI::DocumentViews` avant `find_package(... COMPONENTS ChatUI)`.
Consulter le [manuel d'intégration](Documentation/Documentation.html#/fr/integration).

</details>

---

## Licence

CyMCPSDK utilise la [licence MIT](LICENSE). Copyright © 2026 Cyberalien.
Les moteurs externes, modèles et services fournisseurs gardent leurs conditions.

### Bibliothèques tierces

| Bibliothèque · sources | Licence · texte | Utilisation / périmètre |
| --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json) 3.12.0 | [MIT](third_party/nlohmann/LICENSE.MIT) | Types JSON et validation du SDK |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) 0.66.0 | [MIT](third_party/httplib/LICENSE) | Serveur HTTP local ; transports de test de développement |
| [PicoSHA2](https://github.com/okdshin/PicoSHA2) | [MIT](third_party/picosha2/LICENSE) | Empreintes des artefacts |
| [libcurl](https://curl.se/libcurl/) | [Licence curl et notices TLS](THIRD_PARTY_NOTICES.md) | Dépendance hôte HTTP(S) Linux/macOS |
| [CyUI](https://github.com/MrMybal/CyUI), Dear ImGui, MD4C | [Notices MIT](THIRD_PARTY_NOTICES.md) | Modules UI/Markdown natifs optionnels |
| Lexers Lexilla/Scintilla | [Notices permissives](THIRD_PARTY_NOTICES.md) | Coloration du code CyUI optionnelle |
| JetBrains Mono | [Notice SIL OFL 1.1](THIRD_PARTY_NOTICES.md) | Police CyUI embarquée optionnelle |
| [CyDoc](https://github.com/MrMybal/CyDoc) 0.1.0 | [MIT](Documentation/DocData/CyDoc-MIT.txt) | Lecteur statique du manuel ; aucune dépendance du SDK natif |
| marked, PrismJS, esbuild | [Notices MIT](THIRD_PARTY_NOTICES.md) | Génération de documentation uniquement |

Conserver les [notices tierces complètes](THIRD_PARTY_NOTICES.md) et le dossier `licenses/`
fourni avec les distributions de l'application. Ce tableau est un index de leurs conditions.

## Références et liens

- [Manuel français](Documentation/Documentation.html#/fr/overview)
- [Notes de version et problèmes connus](VERSION.fr.md)
- [Dépôt des sources](https://github.com/MrMybal/CyMCPSDK)
- [Suivi des problèmes](https://github.com/MrMybal/CyMCPSDK/issues)

<details>
<summary><strong>AI Usage</strong></summary>

- [Prompt d’intégration](Docs/IntegrationPrompt.fr.md) et [prompt de mise à jour](Docs/UpdatePrompt.fr.md).
- [Guide IA principal](Documentation/DocAI.md) — architecture, compilation et contrats d'intégration.
- [Intégration détaillée](Docs/Integration.md) — référence développeur française existante.
- [Profils fournisseurs](Docs/Providers.md) — référence française existante des transports.

</details>
