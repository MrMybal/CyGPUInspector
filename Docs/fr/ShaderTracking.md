# Suivi des shaders

*Disponible aussi en [anglais](../ShaderTracking.md), qui fait foi.*

**État : implémenté (milestone 1).**

## Capture du bytecode

À `init_pipeline`, ReShade fournit les sous-objets du pipeline. Pour chaque sous-objet de type
shader, `shader_desc::code` / `code_size` donnent le bytecode compilé. **Ce pointeur n'est valide
que pendant le callback** : `ShaderTracker::RegisterShader` copie les octets immédiatement.

Étages suivis : vertex, hull, domain, geometry, pixel, compute, amplification, mesh, raygen,
any hit, closest hit, miss, intersection, callable — selon ce que l'API expose réellement.

## Signature

Deux empreintes sont calculées :

| Empreinte | Calcul | Sert à |
|---|---|---|
| `signature` | SHA-256 du conteneur complet, **champ checksum mis à zéro** | identifier le shader à l'octet près ; clé de la base et des remplacements |
| `semantic_hash` | SHA-256 de la **partie code seule** (`SHEX` / `SHDR` pour DXBC, `DXIL` pour SM6) | rapprocher des variantes qui ne diffèrent que par des parties de debug ou de réflexion |

Le checksum du conteneur DXBC est dérivé du reste et écrit différemment selon les compilateurs :
l'inclure rendrait la signature instable sans rien apporter. C'est vérifié par un test.

Le conteneur donne aussi l'étage réel et le shader model, lus dans le jeton de version en tête de
la partie code : bits 16+ = type de programme, bits 4–7 = majeur, bits 0–3 = mineur. Le type de
sous-objet ReShade n'est utilisé qu'en repli (GLSL et SPIR-V ne portent pas de type de programme).

## Pipelines

`init_pipeline` donne aussi le handle du pipeline final. La table `handle → PipelineRecord` permet
à `bind_pipeline` de résoudre en O(1) quels shaders un draw va exécuter — c'est le seul coût par
bind, et il n'y a aucun coût par draw.

En D3D11 un « pipeline » ReShade est un seul objet d'état (un `ID3D11PixelShader`), donc un
pipeline porte un shader. En D3D12 un `ID3D12PipelineState` porte VS+PS+… : le mapping est 1..N.

Les `PipelineRecord` ne sont **jamais supprimés**, même à `destroy_pipeline` : seule l'entrée de la
table de handles part. Les événements des frames précédentes référencent encore ces identifiants.

## Corrélation shader → draw calls

Côté standalone, à la fin de chaque frame, chaque événement de draw ou dispatch est attribué à tous
les shaders de son pipeline. On obtient par shader : draws de la frame, dispatches de la frame,
total cumulé, et la liste des index d'événements (`EventsUsingShader`) — ce qui permet de
sélectionner un shader et de voir ses 381 draw calls (§19 du cahier des charges).

## Ce qui manque encore

* Réflexion des bindings (SRV / UAV / CBV par shader) : nécessite `D3DReflect` (DXBC) ou
  `IDxcUtils::CreateReflection` (DXIL), côté standalone — milestone 5.
* Persistance par signature dans la base — voir [Database.md](Database.md).
* Suivi des shaders de bibliothèque / ray tracing au-delà de l'enregistrement de leur bytecode.
