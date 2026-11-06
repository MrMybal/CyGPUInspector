---
id: licenses
title: Licenses and distribution
description: Find the original product and dependency notices for redistribution.
section: Reference
order: 18
revision: 26037358602c
---

# Licenses and distribution {#licenses-and-distribution}

## Product license {#product-license}
CyGPUInspector is GNU AGPL v3 or later. See the full [product license](assets/notices/product-license.txt). A brief explanation here does not replace the actual terms. Redistributing modified binaries may require providing corresponding source under those terms.

## Third-party components {#third-party-components}
The application includes permissively licensed components (including CyUI and Dear ImGui), GPL shader tooling and separately licensed official ReShade/DXC components. They retain their own copyright and permission notices. The [component inventory](doc:third-party) identifies their use and provenance. The complete texts are included in LICENCES in the package and with separately copyable Unreal plugins where applicable.

CyUI 1.2.0 uses the MIT license. Its [upstream license](https://github.com/MrMybal/CyUI/blob/ff023e646c6a3038b3dfbfd53f6d2b4a2f5955e3/LICENSE) is in a currently private repository; repository access is not needed to read the [complete included MIT text](assets/notices/CyUI/LICENSE.txt). Keep that text and the syntax-engine notices when redistributing the compiled integration.

License terms are provided in their original English text rather than translated into an unofficial replacement. This French/English guide explains where to find them. DXIL validation/signing and the ReShade runtime have their own distribution conditions; see the detailed inventory before assembling another package.

## Data and providers {#data-and-providers}
The offline help reader needs no account. The application’s own assistant and MCP implementation do not depend on CyToolsCore or CyUEMCPSDK. External provider accounts, CLI/API availability and usage terms remain separate from the application license. Local help content is not sent to a provider by opening this guide.
