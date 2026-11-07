# CyGPUInspector showcase

Maintained source for the bilingual application presentation. The generated static output is
`build/showcase/site/`, separate from the offline application's `Documentation/` directory.
Both remain local until publication is requested. Existing Pages automation is unchanged.

## Build

From the application root, with Node.js 22+:

```sh
npm --prefix .CyDocBuild ci
node website/build.mjs
node website/verify.mjs
```

The build invokes the application's pinned CyDoc compiler, verifies and copies its **complete**
export into `manual/`, and emits static HTML, a classic script and local hashed assets.
It needs no Git metadata, release API, account or network request after dependencies are installed.
Edit `content.mjs`, `styles.css` and `main.js`; do not edit generated files. `content.mjs`
contains the verified release destinations and complete English/French visitor copy.

Open `build/showcase/site/index.html` directly. English is the default; `?lang=fr` selects French.
`manual/Documentation.html#/fr/welcome` is the actual French CyDoc reader, not Markdown.
The root `Documentation.html` alias and old `#/en/...` / `#/fr/...` entry fragments remain compatible.
All required resources and legal texts travel with the output; remote GitHub/download actions
are optional destinations deliberately visited by the user. No application ZIP is bundled.

## Preview and checks

```sh
node website/preview.mjs --port=43763 --base=/CyGPUInspector/
node website/tests.mjs
```

Preview: `http://127.0.0.1:43763/CyGPUInspector/`. The server binds only to loopback and exposes
only the final static output. Stop it with Ctrl+C. The same output works from a domain root,
a project subpath or a moved local folder without server rewrites.

Browser regression checks reuse the existing Playwright development dependency in `.CyDocBuild/`.
They exercise direct file opening, translations, the viewer, keyboard focus, scroll restoration,
mobile navigation and first-screen geometry. On Windows they use installed Google Chrome;
`CYGI_TEST_BROWSER` selects a different executable. Install managed Chromium only if needed.
Viewport simulation is not a native mobile-device test. Working notes and QA captures belong
under `.CyDocBuild/internal/`, never in the human manual or the website navigation.
Reflow at an effective 640 × 400 CSS viewport simulates a 1280 × 800 window at 200% zoom;
it is not a claim that the operating system's or browser's zoom setting was exercised.
Tests fail foreign runtime requests by default. If local browser/security instrumentation
injects its own requests, `CYGI_TEST_ENVIRONMENT_ORIGINS` may explicitly list their exact
origins; these are recorded separately in private results. This does not disable that instrumentation.

`verify.mjs` validates the final inventory, hashes, relative resources, manual and EN/FR route IDs.
Source/style/script/image changes produce new content-hashed URLs in the same build. HTML must
be revalidated by any future static host; no service worker or persistent HTML cache is installed.

## Captures and licensing

`captures.json` records genuine synthetic-session application views and real CyDoc manual views.
The app captures are from interface 0.4.0; the manual is the actual 0.4.1 documentation.
They are illustrations, not benchmarks. No UI is generated or composited.

Existing branding and application assets retain the project's AGPL-3.0-or-later license.
The complete application license is copied to `LICENSE.txt`; the full CyDoc export retains its
own MIT notices and generation-dependency notices. No CyDoc Studio/runtime or media modules
are added. Only the application's existing image module is distributed.

## Optional publication

This task prepares a local commit, not a deployment. After explicit publication authorization,
adapt the existing Pages workflow to upload only this generated site; preserve its root reader
aliases and the application's offline help. Do not upload authoring inputs, profiles, databases,
private work records or build services. No such workflow change is part of this local setup.
