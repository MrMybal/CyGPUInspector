# Documentation toolchain

The human manual is authored in `manual/en/` and `manual/fr/`. Both editions are required; English is the original and default. Keep page IDs and explicit anchors identical across locales. `assets/` contains product-owned media and original license notices. `api/` contains reviewed neutral API declarations and their translations, imported with CyDoc's public API. No source-file index is distributed.

CyDoc 0.1.0 is pinned at revision `6ddf9a582defb2f11fbb3f992afef6f98dcadc93` in `vendor/ORIGIN.json`. Its archive was built from that checkout with `npm ci` and `npm pack`; it is not fetched from the npm registry. Only the image module is enabled. Static SVG diagrams require no graph module. No Three.js dependency is installed in this application toolchain.

From the repository root, with Node.js 22+:

```sh
npm --prefix .CyDocBuild ci
npm --prefix .CyDocBuild run check
node .CyDocBuild/check.mjs Documentation
npm --prefix .CyDocBuild test
```

The direct reader is `Documentation/Documentation.html`. Its local resources are in `Documentation/DocData/`; `Documentation/DocAI.md` is a separate agent sidecar. Neither this workspace nor its dependencies are distributed. Do not edit generated files. `check` also regenerates the export.

An English body edit requires review of the French counterpart. Set `revision` on the English page and `sourceRevision` on the French page to the first 12 hexadecimal characters of SHA-256 of the English body (after its frontmatter, with LF line endings). The build checks this pair so an unnoticed English edit cannot publish a stale translation. Metadata-only descriptions/titles must also be translated and reviewed.

CMake invokes `build.mjs --output bin/<configuration>/Documentation`. The release packager regenerates this output and verifies every delivered file and source hash in both the staged tree and ZIP. It does not package authoring inputs or internal reports.

The existing Pages workflow uses the same compiler and enables the root compatibility redirect only in its hosted artifact:

```sh
npm --prefix .CyDocBuild run build:pages
npm --prefix .CyDocBuild run check:export
```

The hosted artifact is `.CyDocBuild/internal/pages/site/`. Only that export is uploaded. Existing README URLs keep their fragments through `index.html`; the real reader is `Documentation.html`. Deployment runs after an authorized push, not as part of a local build.

`internal/` is ignored by Git and outside content roots. Keep private notes, previous outputs, upstream checkouts, screenshots and test results there. Native entry-point diagnostics: `python .CyDocBuild/tests/native.py --output .CyDocBuild/internal/native-<unique-name>`. Packaging regression checks: `python -m unittest discover -s Tests -p test_package_release.py`.
