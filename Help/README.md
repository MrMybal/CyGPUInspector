# CyDoc offline documentation

## Reader and content
CyDoc 0.1.0 is pinned at revision 774f2a7eabf7dd181a98ffc5add115b580b67f42.
The MIT archive and official C++ route helper are in ThirdParty/CyDoc. The product
remains Windows x64 / Win32 / D3D11. The complete reader opens through file:// in
a modern browser, with no server or CDN. English is the default and French is included.
Manual and catalog.json preserve the existing content. Media retains the captures;
diagrams.json compiles static SVG diagrams. Only image is enabled: no Three.js,
mesh, video, GIF, gallery or interactive graph is distributed.

## Regenerate
Node.js 22 or later is needed only during generation. From the application root:

```sh
npm --prefix Documentation ci
npm --prefix Documentation run build
npm --prefix Documentation run check
node Tools/build_help.mjs --output bin/Release/Help
```

build.cmd Release invokes this generator through CMake. cydoc.config.json is the
source configuration. Fresh build-time inputs under build/ contain frontmatter,
explicit shared anchors and rewritten local links. The official CyDoc CLI compiles
and validates them. The whole output is copied beside the EXE; previous outputs
are retained under build/cydoc-migration. Never hand-edit generated data.js,
manifest.json or bundles. The public viewer embedding selects Nord, a product
storage namespace and the exporter's classic-script local resource providers.

## Translation and source scope
translations.json records the English revision reviewed for each French page.
An English edit marks its translation outdated. Only after reviewing/updating it:

```sh
node Tools/build_help.mjs --accept-translations
```

Every build writes translation-report.json and a bilingual status page. Missing
translations fall back to English. Legal notices remain in original English.
The lexical source index includes only public Localization, ModPackage, Version,
InProcessApi and HelpLauncher headers. Comments and includes are indexed, not C++ semantics,
macros, provider implementations or private configuration.

## Native integration and checks
F1 opens the focused view's topic; Help > User guide opens welcome. Existing page
IDs alias to lowercase IDs and legacy headings use legacy-routes.json.
--help-language and --help-topic remain supported, including #section and
?media=figure-0. --help-url-file writes the resolved file URL instead of launching
it for automated diagnostics. The browser owns reading positions and keyboard/touch
controls. Enlargement stays inside its window; Escape reduces it.

```sh
npm --prefix Documentation test
python -m unittest discover -s Tests -p test_package_release.py
```

Packaging verifies every export file using delivery.json and rejects missing or
stale resources. Tests use file URLs and block network requests. Persistence of
personal reading data depends on browser file-origin storage; reading needs none.

## GitHub Pages
The hosted reader is available at [the English manual](https://mrmybal.github.io/CyGPUInspector/#/en/welcome) and [the French manual](https://mrmybal.github.io/CyGPUInspector/#/fr/welcome). It uses the same complete CyDoc export as the offline application.

```sh
npm --prefix Documentation run build:pages
npm --prefix Documentation run check:export
```

.github/workflows/documentation.yml builds on documentation changes to main, validates pull requests, and supports manual runs. It tests both file URLs and HTTP under /CyGPUInspector/ before uploading only build/pages/site through the official Pages artifact/deployment actions. Browser profiles, test output, source archives, dependencies and application binaries are outside that artifact. GitHub Pages must use the GitHub Actions publishing source.

Translation revisions normalize line endings so Windows and Linux builds agree. To test the hosted reader manually, set CYGI_DOCS_TEST_TRANSPORT=http; CYGI_DOCS_URL can instead select the deployed reader's index URL. Personal notes and reading positions remain local to each browser origin; the hosted and file-based readers do not share them.
