# Intégration CyUI

Le standalone inclut **CyUI 1.2.0**, révision
`ff023e646c6a3038b3dfbfd53f6d2b4a2f5955e3`, provenant de
[MrMybal/CyUI](https://github.com/MrMybal/CyUI).
`ThirdParty/CyUI/ORIGIN.txt` décrit le sous-ensemble inclus. Les sources amont
restent inchangées ; ni téléchargement réseau, ni dépôt voisin, ni runtime CyUI
installé ne sont nécessaires à la compilation.

## Modules sélectionnés

| Cible | Usage |
| --- | --- |
| CyUI::Core | Thèmes, densité, DPI, polices, widgets, notifications, langues et menus filtrables |
| CyUI::CodeEditor | HLSL éditable, reconstructions et désassemblage en lecture seule, recherche, marqueurs et Undo/Redo |
| CyUI::Code | Dépendance : syntaxe HLSL commune et cache de tokens |
| CyUI::Timeline | Spans frame/passe/commande et historique GPU, zoom, déplacement et sélection |
| CyUI::Plots | Historique CPU/GPU, budgets et intervalle visible |
| CyUI::Images | Aperçus, image finale, buffers sauvegardés et illustrations de l’aide |
| CyUI::Inspector | État capturé du pipeline, filtrage, entiers exacts, lecture seule |
| CyUI::Diagnostics | Messages de compilation et journal MCP borné/filtrable |
| CyUI::DocumentViews | Conversations Markdown ; le manuel utilise CyDoc |
| CyUI::Canvas | Dépendance : transformations et clipping des vues |

`CYUI_MODULES` est configuré avant `add_subdirectory` ; les cibles explicites
remplacent le lien général. `CYUI_IMGUI_TARGET` fournit le Dear ImGui docking
existant du host, version 1.93 WIP, compatible avec le minimum 1.92. Il n’y a qu’une
instance d’ImGui. L’ancienne copie indépendante `ThirdParty/ImGuiColorTextEdit`
a été supprimée au profit du backend commun.

Demo, Audio, Gallery et WebCEF sont désactivés en production. L’option
`CYGI_BUILD_UI_GALLERY` permet une galerie de développement limitée aux modules
sélectionnés. Aucun runtime de navigateur ou module audio n’est requis.

## Responsabilités conservées

Capture, IPC/MCP, autorisations, appels fournisseurs, durées de vie GPU, conversion
de canaux/profondeur, compilation/remplacement, stockage et persistance restent
dans l’application. Les vues ne modifient pas les mesures ; champs inconnus,
positions estimées, intervalles d’attente, confiances et provenances restent explicites.
La timeline garde les passes en chevauchement, la sélection de frame et de commande.
Son curseur de position remplace l’ancienne barre sur mesure.

La molette zoome ; bouton central et adaptateur host gauche/droit déplacent la vue.
Maj+molette déplace horizontalement ; Ctrl+molette fait défiler les pistes.
Double-clic ajuste la frame ou reprend le suivi. Les tooltips de l’historique
conservent les valeurs complètes lorsque l’échelle percentile coupe un pic.
Les images gardent zoom, déplacement, ajustement et raccourci de prévisualisation.

L’éditeur conserve UTF-8, CRLF, tabulations, absence de saut final, état modifié,
marqueurs du compilateur et historique Undo. Thème/DPI ne rechargent pas le document.
Les diagnostics gardent ligne/colonne ; le désassemblage garde filtre, retour à la
ligne, sélection et copie intégrale. La préparation dépend des révisions, pas de
chaque frame. Au-delà de 32 Mio, la préparation lexicale est désactivée avec un avis,
sans tronquer le texte éditable. Le désassemblage utilise le backend en lecture seule.

`Lang/*.json` reste le format de catalogue du host ; langues ajoutées à l’exécution
et modèle de traduction sont conservés. Le host synchronise CyUI et garde les IDs
anglais masqués des fenêtres. Contenu utilisateur, code, chemins et réponses des
fournisseurs ne sont pas traduits. Les identifiants de conversation survivent aux
ajouts/évictions et ne sont pas envoyés dans les prompts.

Formats du layout, préférences, profils et captures restent inchangés.
Les liens des conversations sont copiés pour examen et ne lancent pas le navigateur.
L’aide locale possède sa propre navigation ; seuls ses liens Web explicitement
cliqués peuvent ouvrir le navigateur.

## Limites et intégrations

Le Markdown natif est un sous-ensemble, pas un moteur complet CommonMark/HTML.
L’aide utilise désormais CyDoc en file:// ; DocumentViews reste utilisé pour le chat.
Tables de ressources/événements, comparaison de captures et liste de passes dérivées
restent des vues métier avec tri, corrélations, confiance et renommage.
Le plugin Unreal conserve Slate et son intégration runtime ; aucune dépendance
Editor ou seconde UI n’est introduite dans ses modules runtime.

## Compilation et vérifications

Le produit vise **Windows x64**. Utilisez Visual Studio 2022 avec outils C++20,
Windows SDK, CMake et Ninja ; `build.cmd` retrouve ces outils. Le socle reste C++17 ;
CodeEditor propage C++20 à ses consommateurs. Aucun build Linux/macOS du produit
Win32/D3D n’est fourni, même si CyUI prend en charge d’autres hôtes.

```powershell
build.cmd Release
python -m unittest discover -s Tests -p test_package_release.py -v
python Tests/check_interface.py --output build/ui-check
```

Les suites natives Core, Assistant, Shader, IPC et Interface utilisent les vrais
adaptateurs et des frames ImGui, sans jeu. Le script de rendu teste l’application
D3D11 sur plusieurs thèmes, densités, DPI et vues avec un producteur synthétique et
des préférences privées. Ces essais ne valident ni un vrai jeu ni un fournisseur payant.

Le cache natif est sous `build/Native-Release`, séparé des archives `build/Release`.
UTF-8 console et langue du compilateur stabilisent les dépendances Ninja, y compris
avec MSVC installé seulement en français. Pour un CMake manuel, utilisez aussi
`chcp 65001`. Une configuration propre utilise un nouveau dossier de build sans
effacer données utilisateur ni anciens paquets. Vérifiez la fermeture des modules
et l’absence de chemin vers un dépôt CyUI voisin.

Les notices complètes MIT CyUI/TextEditor et Lexilla/Scintilla/SciTE sont conservées
dans les deux dossiers de licences de distribution et comparées à leurs sources.
Le produit reste AGPL-3.0-or-later. Le dépôt CyUI est actuellement privé ; le
[texte MIT inclus](../../LICENCES/CyUI/LICENSE.txt) reste consultable localement,
avec son [lien amont](https://github.com/MrMybal/CyUI/blob/ff023e646c6a3038b3dfbfd53f6d2b4a2f5955e3/LICENSE).
