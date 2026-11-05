# Documentation locale CyDoc

## Lecteur et contenu
CyDoc 0.1.0 est fixé à la révision 774f2a7eabf7dd181a98ffc5add115b580b67f42.
Le paquet MIT et le helper C++ officiel sont dans ThirdParty/CyDoc. Le produit
reste Windows x64 / Win32 / D3D11. Le lecteur complet s’ouvre par file:// dans un
navigateur moderne, sans serveur ni CDN. L’anglais est la base et le français est inclus.
Manual et catalog.json conservent les contenus ; Media garde les captures et
diagrams.json compile les SVG statiques. Seul image est activé : ni Three.js,
mesh, vidéo, GIF, galerie ni graphe interactif ne sont distribués.

## Régénération
Node.js 22 ou supérieur sert uniquement à la génération. Depuis le projet :

```sh
npm --prefix Documentation ci
npm --prefix Documentation run build
npm --prefix Documentation run check
node Tools/build_help.mjs --output bin/Release/Help
```

build.cmd Release invoque le générateur par CMake. cydoc.config.json est la
configuration source. Un nouveau projet sous build/ contient métadonnées, ancres
communes et liens convertis ; le CLI officiel CyDoc compile et valide cet ensemble.
Toute la sortie est copiée près de l’EXE ; les précédentes restent sous
build/cydoc-migration. Ne modifiez pas data.js, manifest.json ou les bundles générés.
L’adaptateur de l’API publique choisit Nord, un stockage propre au produit et les
fournisseurs de ressources locaux par scripts classiques prévus par l’exporteur.

## Traduction et périmètre source
translations.json conserve la révision anglaise relue pour chaque page française.
Modifier l’anglais rend la traduction périmée. Après relecture/mise à jour seulement :

```sh
node Tools/build_help.mjs --accept-translations
```

Chaque build écrit translation-report.json et une page d’état bilingue. Un texte
absent utilise l’anglais. Les licences restent dans leur anglais original.
L’index lexical se limite aux en-têtes publics Localization, ModPackage, Version,
InProcessApi et HelpLauncher. Il présente commentaires et includes, pas la sémantique C++, les
macros, les fournisseurs ou configurations privées.

## Intégration native et vérifications
F1 choisit le sujet de la vue active ; Aide > Guide utilisateur ouvre welcome.
Les anciens IDs redirigent vers les minuscules et legacy-routes.json traduit les
anciennes ancres. --help-language et --help-topic restent disponibles avec #section
ou ?media=figure-0. --help-url-file écrit l’URL résolue sans lancer le navigateur
pour les diagnostics. Le lecteur garde positions et interactions clavier/tactiles.
L’agrandissement reste dans la fenêtre ; Échap réduit la vue.

```sh
npm --prefix Documentation test
python -m unittest discover -s Tests -p test_package_release.py
```

Le packaging compare les fichiers à delivery.json et refuse une ressource absente
ou périmée. Les tests utilisent file:// et bloquent le réseau. La persistance des
données personnelles dépend du stockage d’origine fichier ; la lecture n’en dépend pas.

## GitHub Pages
Le lecteur publié est accessible en [anglais](https://mrmybal.github.io/CyGPUInspector/#/en/welcome) et en [français](https://mrmybal.github.io/CyGPUInspector/#/fr/welcome). Il utilise le même export CyDoc complet que l’application hors ligne.

```sh
npm --prefix Documentation run build:pages
npm --prefix Documentation run check:export
```

.github/workflows/documentation.yml construit le manuel lors des changements documentaires sur main, valide les pull requests et accepte un lancement manuel. Il teste file:// et HTTP sous /CyGPUInspector/ avant de téléverser uniquement build/pages/site par les actions officielles de déploiement Pages. Profils de navigateur, résultats de tests, archives source, dépendances et binaires de l’application restent hors de cet artefact. La source de publication GitHub Pages doit être GitHub Actions.

Les révisions de traduction normalisent les fins de ligne pour que Windows et Linux concordent. Pour tester le lecteur hébergé, définissez CYGI_DOCS_TEST_TRANSPORT=http ; CYGI_DOCS_URL permet de choisir l’URL de l’index déployé. Notes et positions de lecture restent locales à chaque origine du navigateur ; les lecteurs Web et fichier ne les partagent pas.
