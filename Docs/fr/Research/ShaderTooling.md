# Recherche — Désassemblage, décompilation et compilation de shaders

*Disponible aussi en [anglais](../../Research/ShaderTooling.md), qui fait foi.*

## 1. Formats rencontrés

| API | Conteneur | Shader Model | Contenu |
|---|---|---|---|
| D3D11 | DXBC | SM 4.0 – 5.1 | bytecode « TokenizedProgramFormat », assembleur lisible, réflexion complète |
| D3D12 | DXBC (conteneur) avec partie **DXIL** | SM 6.0 – 6.9 | LLVM 3.7 bitcode encapsulé, signé |
| Vulkan | SPIR-V | — | module SPIR-V (plus tard) |
| OpenGL | texte GLSL | — | déjà du source (plus tard) |

Les deux premiers sont la cible prioritaire.

## 2. Désassemblage — disponible sans dépendance externe

* **DXBC** : `D3DDisassemble()` de `d3dcompiler_47.dll`, présente sur toute machine Windows 10/11
  et dans le Windows SDK. Donne l'assembleur `ps_5_0 / dcl_… / mad r0.xyzw, …`.
* **Réflexion DXBC** : `D3DReflect()` → `ID3D11ShaderReflection` : bindings de ressources,
  signatures d'entrée/sortie, layout des constant buffers, instruction count. C'est la source des
  métadonnées « ressources lues / écrites » sans avoir besoin de décompiler.
* **DXIL** : `IDxcCompiler3::Disassemble()` de `dxcompiler.dll`. Le Windows SDK installé sur cette
  machine fournit `dxc.exe` + `dxcompiler.dll` + `dxil.dll` en
  `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\`.
* **Réflexion DXIL** : `D3DReflect` ne fonctionne **pas** sur DXIL. Il faut
  `IDxcUtils::CreateReflection()` / `IDxcContainerReflection` (même DLL).

Conclusion : le désassemblage (Milestone 5) ne demande aucune dépendance tierce discutable, et
peut être fait **côté standalone**, hors du jeu.

## 3. Décompilation — état de l'art réel

| Backend | Entrée → sortie | Licence | Qualité | Statut dans le projet |
|---|---|---|---|---|
| **3Dmigoto HLSLDecompiler** | DXBC SM4/SM5 → HLSL | **GPL-3.0** (`LICENSE.GPL.txt`) | La meilleure disponible pour SM5 : contrôle de flux reconstruit, noms de ressources depuis la réflexion | Backend principal DXBC — intégrable directement, CyGPUInspector étant sous AGPLv3, que la section 13 des deux licences permet de combiner avec lui |
| **dxil-spirv + SPIRV-Cross** | DXIL → SPIR-V → HLSL/GLSL | MIT (`LICENSE.MIT`) + Apache-2.0 | Compilable et sémantiquement correct, mais très « machine » : beaucoup de `_123` temporaires, contrôle de flux structuré par l'algorithme de structurisation | Backend principal DXIL |
| **vkd3d-shader** | DXBC → SPIR-V/HLSL | LGPL-2.1 (compatible AGPLv3) | Alternative DXBC, moins lisible | Backend optionnel, candidat pour comparaison |
| **DXC (`dxcompiler`)** | HLSL → DXBC/DXIL, désassemblage, réflexion | NCSA / University of Illinois | — | Toujours utilisé : désassemblage, validation, recompilation |

Une reconstruction IA (Milestone 9) intervient **après** un de ces backends, jamais à leur place.

### Politique multi-backend

Chaque backend produit un artefact **stocké séparément** dans la base :
`decompiled/<backend>-<version>.hlsl`. Aucun résultat n'écrase un autre. L'interface et l'IA
peuvent comparer les sorties, choisir la meilleure reconstruction ou en combiner des fragments.
Un backend est décrit par `{ nom, version, licence, formats d'entrée supportés, shader models }`
et s'enregistre auprès de `CyGPUInspectorDecompiler`.

### Limites honnêtes à documenter dans l'UI

* Le source HLSL original **n'existe pas** dans le binaire : tout ce qui est produit est une
  reconstruction. Les noms de variables, de fonctions et les commentaires sont perdus à la
  compilation.
* DXIL perd encore plus d'information que DXBC (inlining LLVM, SROA, vectorisation détruite) :
  la reconstruction DXIL sera systématiquement moins lisible. Il faut le dire à l'utilisateur au
  lieu de le laisser croire à un échec de l'outil.
* Un décompilateur peut produire du HLSL **qui ne recompile pas**. C'est attendu, pas un bug
  bloquant : l'état de validation fait partie des métadonnées.

## 4. Validation d'une reconstruction

```
HLSL reconstruit → DXC/FXC (même shader model) → bytecode
                                                     ↓
                            désassemblage comparé au désassemblage original
```

Trois niveaux de verdict stockés par shader et par backend :

1. `compile_failed` — le HLSL reconstruit ne compile pas.
2. `compiles` — il compile pour le bon shader model, avec des signatures d'E/S et des bindings
   compatibles.
3. `equivalent_disasm` — le désassemblage recompilé est proche de l'original (comparaison
   normalisée sur les opcodes). Le seul niveau qui autorise à parler de reconstruction fidèle.

## 5. Compilation des shaders de remplacement — piège D3D12

D3D12 **refuse** un shader DXIL non signé si le mode développeur n'est pas actif. La signature
n'est produite que si **`dxil.dll` est présent à côté de `dxcompiler.dll`** au moment de la
compilation. C'est la cause classique de « mon shader de remplacement est rejeté ».
CyGPUInspectorApp vérifiera la présence de `dxil.dll` et l'affichera dans le panneau de
diagnostic. En D3D11 (FXC/DXBC) le problème n'existe pas.

## 6. Ce qu'on vendorise

`ThirdParty/` contiendra, avec licence complète et notice d'origine conservées :

* `reshade/` — en-têtes du SDK (BSD-3-Clause) ✔ déjà présent
* `imgui/` — Dear ImGui docking (MIT) ✔ déjà présent
* `reshade_imgui/` — en-têtes ImGui **à la version de ReShade** pour l'overlay de l'add-on ✔
* `hlsldecompiler/` — décompilateur 3Dmigoto (GPL-3) ✔ intégré
* `dxil-spirv/` + `SPIRV-Cross/` — chaîne DXIL (MIT / Apache-2.0) ✔ intégrés
* `sqlite/` — amalgamation SQLite (domaine public) — Milestone 2

DXC n'est pas vendorisé : on utilise `dxcompiler.dll` / `dxil.dll` du Windows SDK, avec
possibilité de pointer vers une version plus récente dans les préférences.

## Sources

- [bo3b/3Dmigoto — LICENSE.GPL.txt](https://github.com/bo3b/3Dmigoto/blob/master/LICENSE.GPL.txt)
- [HansKristian-Work/dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv)
- [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)
- [microsoft/DirectXShaderCompiler](https://github.com/microsoft/DirectXShaderCompiler)
- [crosire/reshade](https://github.com/crosire/reshade)
- [crossous/DXIL2HLSL](https://github.com/crossous/DXIL2HLSL) — précédent d'une chaîne DXIL → SPIR-V → HLSL
