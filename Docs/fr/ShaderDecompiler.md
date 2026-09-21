# CyGPUInspectorDecompiler

*Disponible aussi en [anglais](../ShaderDecompiler.md), qui fait foi.*

**État : implémenté et testé** (`Tests/CyGPUInspectorShaderTests`, 142 vérifications sur de vrais
shaders compilés). Désassemblage, réflexion, compilation, et **quatre backends de décompilation**
couvrant DXBC (Shader Model 4/5) et DXIL (Shader Model 6).
Recherche détaillée et licences : [Research/ShaderTooling.md](Research/ShaderTooling.md).

## Ce qui fonctionne aujourd'hui

| Fonction | DXBC (SM 4/5) | DXIL (SM 6) |
|---|---|---|
| Désassemblage | `D3DDisassemble` | `IDxcCompiler3::Disassemble` |
| Réflexion (bindings, signatures, thread group) | `D3DReflect` | `IDxcUtils::CreateReflection` |
| Compilation | `D3DCompile` | `IDxcCompiler3::Compile` |

Les deux DLL sont chargées **à l'exécution** : `d3dcompiler_47.dll` est toujours présente sur
Windows, `dxcompiler.dll` est cherchée à côté de l'exécutable, puis dans le Windows SDK, puis sur
le PATH. Son absence n'empêche pas l'application de démarrer : elle est signalée dans l'onglet
`Tools` du panneau de code, avec le chemin exact de ce qui a été trouvé.

Tout ceci tourne dans le standalone, sur un thread de fond, jamais dans le jeu. Le résultat est
mis en cache par signature, en mémoire et sur disque, donc un shader déjà rencontré dans un autre
lancement ne coûte rien.

## Pipeline

```
Bytecode (DXBC / DXIL)
      ↓  désassemblage              d3dcompiler (DXBC) / dxcompiler (DXIL)
  Disassembly
      ↓  décompilation              plusieurs backends, en parallèle
  Pseudo-HLSL (un fichier par backend)
      ↓  validation                 recompilation DXC / FXC + comparaison de désassemblage
  Verdict par backend
      ↓  nettoyage IA (facultatif)
  HLSL reconstruit, étiqueté
```

L'IA n'est **jamais** le premier décompilateur : elle intervient sur un pseudo-HLSL déjà produit et
validé par un vrai backend.

## Architecture multi-backend

Un backend se décrit par `{ nom, version, licence, formats d'entrée, shader models supportés }` et
s'enregistre auprès du décompilateur. Les résultats sont **stockés séparément**, jamais écrasés :

```
shaders/<signature>/decompiled/<backend>-<version>.hlsl
shaders/<signature>/decompiled/<backend>-<version>.json   verdict, durée, messages
```

C'est ce qui permet de comparer les sorties, de choisir manuellement la meilleure reconstruction, et
de laisser une IA sélectionner ou combiner les fragments les plus fiables.

| Backend | Entrée | Licence | État |
|---|---|---|---|
| **`hlsldecompiler`** (3Dmigoto) | DXBC SM 4/5 | GPL-3.0, vendorisé | **livré** |
| **`cygi-dxbc` 0.1** | DXBC SM 4/5 | AGPL-3.0 (le nôtre) | **livré** |
| **`dxbc-spirv`** (+ SPIRV-Cross) | DXBC SM 4/5 | MIT + Apache-2.0, vendorisés | **livré** |
| **`dxil-spirv`** (+ SPIRV-Cross) | DXIL SM 6.x | MIT + Apache-2.0, vendorisés | **livré** |
| `vkd3d-shader` | DXBC | LGPL-2.1 | **non constructible ici**, voir plus bas |

### `hlsldecompiler` — 3Dmigoto, vendorisé

Le meilleur décompilateur DXBC open source. Il reconstruit les **expressions**, récupère les noms
des ressources, des constant buffers **et de leurs membres** depuis les tables de réflexion, et
rebâtit le contrôle de flux. Sur le shader de tonemap des tests :

```hlsl
cbuffer Tonemap : register(b0)
{
  float Exposure : packoffset(c0);
  float WhitePoint : packoffset(c0.y);
  float2 InvResolution : packoffset(c0.z);
}
SamplerState LinearClamp_s : register(s0);
Texture2D<float4> SceneColor : register(t0);
...
  r0.xy = InvResolution.xy + v1.xy;
  r0.xyz = SceneColor.Sample(LinearClamp_s, r0.xy).xyz;
  r0.xyz = Exposure * r0.xyz;
```

Verdict de validation : `compiles, equivalent disassembly, opcode similarity 100 %`.

Les sources sont vendorisées **sans aucune modification** dans `ThirdParty/hlsldecompiler/`, de
sorte qu'un `diff` avec le dépôt amont reste vide et que la conformité GPL soit vérifiable. Tout ce
qu'il a fallu ajouter est à côté, dans `shim/`, et est marqué comme n'appartenant pas à 3Dmigoto :
quatre symboles seulement (`LogInfo`, `LogDebug`, `LogTime`, `VER_FILE_VERSION_STR`). Les options de
compilation sont relâchées pour ces fichiers uniquement, plutôt que de les corriger. Voir
`ThirdParty/hlsldecompiler/ORIGIN.md` et [THIRD-PARTY.md](../../THIRD-PARTY.md).

Un piège qui a coûté un échec au premier essai : ce décompilateur parse le **texte** de
l'assembleur, et les numéros d'instruction et offsets d'octets que notre visualiseur affiche le
font échouer sur « No opcode ». `Disassemble()` a donc deux styles, `annotated` pour la lecture et
`plain` pour les décompilateurs.

### `cygi-dxbc` — pourquoi le garder à côté de 3Dmigoto

Il traduit l'assembleur DXBC **instruction par instruction** en HLSL et reconstruit les
déclarations depuis la réflexion. Sa sortie est moins lisible que celle de 3Dmigoto, et c'est
exactement pourquoi elle est conservée : elle suit le désassemblage ligne à ligne, donc elle sert
de référence pour vérifier ce que l'autre backend a réarrangé. Deux reconstructions qui divergent
sur un shader sont une information, pas un problème — c'est tout l'intérêt du multi-backend.
Validation sur le même shader : `compiles, equivalent disassembly, opcode similarity 100 %`.

Ce qu'il **ne** fait **pas**, délibérément, et que d'autres backends feront mieux :

* il ne reconstruit pas les expressions : une instruction reste une ligne ;
* il ne retrouve ni noms, ni structures, ni algorithmes ;
* il conserve la forme du contrôle de flux au lieu de le restructurer ;
* les comparaisons écrivent `1.0 / 0.0` là où le matériel écrit un masque de bits — même
  comportement à travers `movc` et les conditions, et c'est dit dans les notes du résultat.

Les instructions qu'il ne sait pas traduire sont laissées **en commentaire** et comptées, pas
silencieusement omises.

CyGPUInspector est publié sous **AGPL v3** et 3Dmigoto sous **GPL v3** ; la section 13 des deux
licences permet de les lier dans un même programme, donc le décompilateur est intégré
directement, avec ses copyrights, notices et sources conservés, et ses fichiers restent sous GPL v3.

### `dxbc-spirv` — la troisième opinion sur DXBC

Le brief demandait ici **`vkd3d-shader`**. Il n'est pas constructible avec cette chaîne d'outils, et
la section suivante dit exactement pourquoi. Ce backend le remplace en couvrant le même terrain :
un décompilateur DXBC d'une **lignée entièrement différente** des deux autres, pour que les
désaccords entre reconstructions veuillent dire quelque chose.

**dxbc-spirv** (MIT, Philip Rebohle) est le nouveau frontal DXBC de DXVK. Ce n'est pas un
traducteur de texte : il analyse le byte code, l'abaisse dans une **représentation intermédiaire
SSA**, y fait tourner de vraies passes d'optimisation, puis émet du SPIR-V. SPIRV-Cross fait le
reste. Sur le shader de tonemap des tests, en `ps_5_0` :

```hlsl
cbuffer Tonemap : register(b0)
{
    float2 Tonemap_1_m0 : packoffset(c0);
    float2 Tonemap_1_m1 : packoffset(c0.z);
};
SamplerState LinearClamp : register(s0);
Texture2D<float4> SceneColor : register(t0);
...
    float4 _48 = SceneColor.Sample(LinearClamp, float2(TEXCOORD.x + Tonemap_1_m1.x, ...));
```

Verdict : `compiles, equivalent disassembly, opcode similarity 100 %`.

Il ne coûte **aucune dépendance nouvelle** : dxil-spirv en dépend déjà, donc les sources étaient là.
Seul l'émetteur SPIR-V manquait à la compilation, ajouté dans une cible à nous plutôt qu'en
modifiant le `CMakeLists.txt` vendorisé de dxil-spirv, qui doit rester identique à l'amont.

Un obstacle réel a dû être franchi, et il est gardé : dxbc-spirv déclare **toujours** le modèle
d'adressage `PhysicalStorageBuffer64` et le modèle mémoire Vulkan, parce qu'il vise Vulkan.
SPIRV-Cross refuse d'émettre du HLSL depuis autre chose que le modèle `Logical`. Le backend
réécrit donc ces déclarations — **mais seulement après avoir vérifié que le module n'utilise
réellement aucun pointeur physique** : pas de `OpTypePointer` en `PhysicalStorageBuffer`, pas de
conversion entier ↔ pointeur. Si le shader en utilise un, le backend **abandonne en le disant**.
Réécrire un module qui a besoin de l'adressage physique produirait une reconstruction silencieusement
fausse, ce qui est pire que pas de reconstruction du tout.

### `vkd3d-shader` — pourquoi il n'est pas là

Ce n'est pas un choix, c'est un constat, vérifié sur le dépôt
[wine/vkd3d](https://gitlab.winehq.org/wine/vkd3d) au commit `f714cb80`.

`libvkd3d-shader` est du C sous autotools, prévu pour GCC et MinGW. Sept en-têtes dont il dépend
n'existent pas dans l'arbre : ils sont **générés**, par quatre générateurs différents dont aucun
n'est disponible ici et dont aucun n'est un outil Windows :

| Manquant | Généré par |
|---|---|
| `config.h` | autoconf |
| `spirv_grammar.h` | le script `make_spirv` du dépôt |
| `vkd3d_version.h` | le Makefile |
| `vkd3d_d3d12shader.h`, `vkd3d_d3d11shader.h`, `vkd3d_d3d10shader.h`, `vkd3d_d3d10_1shader.h` | **widl**, le compilateur IDL de Wine |
| `hlsl.tab.c`, `hlsl.yy.c`, `preproc.tab.c`, `preproc.yy.c` | **flex** et **bison** |

S'y ajoutent `<pthread.h>` et `<unistd.h>`, inclus par `vkd3d-common`, qui n'existent pas sous
MSVC. L'intégrer ne serait pas une affaire de shim comme pour 3Dmigoto : il faudrait réécrire son
système de construction et substituer des en-têtes générés, c'est-à-dire **forker le projet** — ce
qui détruirait précisément la propriété qui rend notre conformité de licence vérifiable, à savoir
qu'un `diff` avec l'amont reste vide.

Le jour où l'un de ces verrous saute (des sources pré-générées publiées en amont, ou une chaîne
Unix acceptée comme prérequis de construction), le backend se branche comme les autres : le
registre est fait pour ça. En attendant, `dxbc-spirv` rend le service attendu, sous une licence
plus permissive et sans rien ajouter à l'arbre.

### `dxil-spirv` — la chaîne Shader Model 6

Il n'existe pas de décompilateur DXIL direct qui vaille la peine. Ce qui existe, et que toutes les
couches de traduction Direct3D 12 sous Linux utilisent, c'est **dxil-spirv** (MIT, Hans-Kristian
Arntzen) : il lit le bitcode LLVM dont un shader SM 6 est fait et produit du SPIR-V. **SPIRV-Cross**
(Apache-2.0 / MIT, Khronos) fait ensuite le chemin SPIR-V → HLSL. Deux sauts, deux projets éprouvés
sur de vrais jeux, et rien de nous entre les deux.

Le détour a un prix, et il faut le dire à l'utilisateur : le SPIR-V intermédiaire est un module
**Vulkan**, qui ne transporte ni les noms de ressources ni les registres Direct3D. Les deux sont
remis en place après coup :

* **les registres** — dxil-spirv, laissé à ses réglages par défaut, projette une liaison D3D sur
  une liaison Vulkan à l'identique : `descriptor set` = `register space`, `binding` = numéro de
  registre. Relire les décorations du SPIR-V redonne donc exactement les registres d'origine ;
* **les noms** — dxil-spirv les jette, il n'en a pas l'usage. Ceux qu'on réaffiche viennent des
  **tables de réflexion du shader lui-même**, lues par DXC, appariées par (espace, registre).
  Rien n'est inventé : une ressource que la réflexion ne nomme pas garde son numéro ;
* **les sémantiques d'entrée de vertex shader** — même principe, depuis la signature d'entrée.

Sur le shader de tonemap des tests, en `ps_6_0` :

```hlsl
cbuffer Tonemap : register(b0, space0)
{
    float4 Tonemap_1_m0[1] : packoffset(c0);
};
Texture2D<float4> SceneColor : register(t0, space0);
SamplerState LinearClamp : register(s0, space0);
...
    float4 _44 = SceneColor.Sample(LinearClamp, float2(Tonemap_1_m0[0u].z + TEXCOORD.x, ...));
```

Verdict : `compiles, equivalent disassembly, opcode similarity 98 %` (100 % sur le compute shader
des tests).

Ce que le détour détruit, et que le champ `notes` du résultat énonce :

* les **membres** d'un constant buffer disparaissent, écrasés dans le tableau de `float4` qu'est un
  uniform block Vulkan — `Exposure` et `WhitePoint` deviennent `.x` et `.y`. C'est la perte la plus
  visible face à 3Dmigoto sur SM 5 ;
* les sémantiques d'interpolants deviennent `TEXCOORD<n>`, SPIR-V ne les transportant pas ;
* le contrôle de flux est celui que le *structurizer* a reconstruit, pas celui qui a été écrit ;
* les temporaires sont numérotés.

Le point d'entrée est émis sous le nom `main` et non sous son nom réel, parce que c'est ce que la
validation par recompilation suppose ; le vrai nom est rappelé dans les notes plutôt que perdu.

Sources vendorisées **sans modification** dans `ThirdParty/dxil-spirv/` et `ThirdParty/SPIRV-Cross/`,
cette dernière épinglée sur le commit que dxil-spirv utilise lui-même, pour consommer le couple que
l'amont teste ensemble. Seul le convertisseur est construit : pas d'outils en ligne de commande, et
le lecteur de bitcode intégré plutôt qu'un LLVM complet — c'est la différence entre 13 Mo de
dépendance et un gigaoctet. Voir les `ORIGIN.md` et [THIRD-PARTY.md](../../THIRD-PARTY.md).

## Validation

Trois verdicts, stockés par shader et par backend :

1. `compile_failed` — le HLSL reconstruit ne compile pas.
2. `compiles` — il compile pour le bon shader model, signatures d'E/S et bindings compatibles.
3. `equivalent_disasm` — le désassemblage recompilé est proche de l'original (comparaison
   normalisée sur les opcodes). Le seul verdict qui autorise à parler de reconstruction fidèle.

## Piège D3D12

D3D12 refuse un shader DXIL non signé hors mode développeur. La signature n'est produite que si
`dxil.dll` est présent à côté de `dxcompiler.dll`. Le standalone vérifiera sa présence et
l'affichera dans son panneau de diagnostic plutôt que de laisser échouer un remplacement sans
explication.

## Étiquetage

Trois niveaux, toujours distingués dans l'interface (§31) :

* `Original source` — jamais disponible, n'existe pas dans le binaire ;
* `Decompiler reconstructed HLSL` — sortie d'un backend, avec son nom et sa version ;
* `AI reconstructed HLSL` — sortie d'un modèle, avec son nom et la version du prompt.
