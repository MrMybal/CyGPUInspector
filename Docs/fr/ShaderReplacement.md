# Contrôle runtime des shaders

*Disponible aussi en [anglais](../ShaderReplacement.md), qui fait foi.*

**État : implémenté et testé** (milestone 7).

## Disable — implémenté

L'add-on maintient une table plate `pipeline_id → désactivé` (131072 entrées, octets atomiques,
sans verrou). Les callbacks `draw`, `draw_indexed`, `dispatch` et `draw_or_dispatch_indirect`
retournent `true` — ce qui supprime la commande — quand le pipeline lié est marqué.

* Désactiver un shader marque tous les pipelines qui l'utilisent, y compris ceux créés plus tard
  (`OnPipelineRegistered` hérite de l'état).
* L'événement est quand même enregistré, avec le drapeau `kEventSkipped` : le standalone voit donc
  ce qui a été supprimé, il n'y a pas de trou silencieux dans la frame.
* Quand rien n'est désactivé, un seul `atomic<bool>` court-circuite tout le mécanisme : le coût sur
  le chemin chaud est nul en usage normal.

C'est le test le plus rapide pour confirmer ce qu'un shader produit à l'écran (§54, §84).

## Replace — implémenté

```
Original → édition HLSL (standalone) → FXC/DXC → bytecode → pipe de contrôle
        → pipeline de remplacement reconstruit (add-on) → échangé au moment du draw
```

Trois obstacles réels, et ce qui a été fait :

**1. `create_pipeline` ne permet de substituer un shader qu'à la création.** Trop tard pour un
shader créé il y a dix minutes. La description de **chaque** pipeline est donc capturée à
`init_pipeline` (`PipelineBlueprint`), ce qui permet d'en rebâtir une variante à la demande. Le
bytecode n'y est pas dupliqué — il vit déjà une fois par signature dans le `ShaderTracker` — sans
quoi un vrai jeu coûterait des centaines de mégaoctets.

**2. `bind_pipeline` est un événement `void`** : impossible de l'annuler pour y substituer autre
chose. L'échange se fait donc **au moment du draw**, qui lui retourne un `bool` : l'add-on lie le
pipeline de remplacement, émet lui-même le draw, remet l'original, et supprime la commande du jeu.

**3. Une commande émise par l'add-on repasse par les hooks de ReShade**, donc par nos propres
callbacks. Une garde de réentrance en `thread_local` coupe la récursion.

Un pipeline dont un sous-objet n'est pas copiable est marqué **non remplaçable avec sa raison**,
plutôt que d'échouer silencieusement.

`Restore` retire la publication du handle *avant* de détruire le pipeline : un draw ne doit jamais
voir un handle déjà libéré.

## Highlight — implémenté, comme un remplacement

Highlight n'est pas un mécanisme séparé : le standalone **génère** un pixel shader qui écrit du
magenta sur toutes les cibles déclarées par l'original — la signature de sortie vient de la
réflexion, ce qui garantit la compatibilité avec le pipeline — le compile, et l'envoie comme
remplacement. L'add-on n'embarque donc toujours aucun compilateur.

## État persistant

Par signature, dans le profil du jeu : `disabled`, `highlighted`, tag, nom donné par l'utilisateur,
shader de remplacement. Réappliqué au lancement suivant (§38).

## Interface

Onglet HLSL : éditeur sur la reconstruction du backend choisi, puis `[ Compile ]`,
`[ Compile & Inject ]`, `[ Reset to backend output ]`, `[ Restore original ]`.
Onglet Shader : `Disable` / `Enable`, `Highlight`, `Restore`.
Les draws passés par un remplacement reviennent marqués `replaced` dans la liste des événements.

## Limites

* Le temps d'un draw remplacé inclut la liaison du pipeline de remplacement et le retour à
  l'original : deux `bind_pipeline` de plus par draw concerné. C'est un mode de diagnostic.
* Un pipeline utilisant un sous-objet non copiable n'est pas remplaçable (la raison est affichée).
* En D3D12, le profil du shader de remplacement doit être signé : voir le piège `dxil.dll` dans
  [ShaderDecompiler.md](ShaderDecompiler.md).
