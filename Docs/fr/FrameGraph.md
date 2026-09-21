# Frame graph et graphe de dépendances

*Disponible aussi en [anglais](../FrameGraph.md), qui fait foi.*

**État : implémenté et testé** (`Tests/CyGPUInspectorIpcTests`).

Aucun jeu ne fournit de nom de passe : l'API add-on ReShade n'expose ni `PIXBeginEvent` ni
`ID3DUserDefinedAnnotation` (voir [Research/ReShadeAddonAPI.md](Research/ReShadeAddonAPI.md) §7).
Le graphe est donc **reconstruit** à partir des dépendances, côté standalone, sur les événements
déjà reçus.

## Étape 1 — clustering en passes

Des événements consécutifs forment une passe tant que leur **clé de regroupement** ne change pas :

| Type de commande | Clé |
|---|---|
| draw | render target 0 + cible de profondeur |
| dispatch | la ressource qu'il écrit quand elle est connue, et **une barrière termine la passe** |
| copie / résolution / clear | destination + source |

Les événements de binding (`bind_render_targets`, `begin_render_pass`) ne forment pas de passe :
ils mettent à jour le jeu de render targets courant. C'est indispensable, parce qu'un événement de
draw ne transporte que le slot 0 — sans l'événement de binding, un GBuffer serait indétectable.

La clé des dispatch n'inclut délibérément **pas** le pipeline. Regrouper par pipeline se lit bien
et se révèle faux sur un vrai moteur : une chaîne de post-traitement, c'est vingt pipelines compute
différents à la suite, et sur un vrai jeu D3D12 cela sortait en vingt passes d'une commande
chacune, autrement dit une liste qui ne dit rien. Ce qui sépare réellement deux passes compute,
c'est la **barrière** entre elles — un moteur qui dit que la seconde lit ce que la première a
écrit — et c'est donc elle qui termine une passe ici. Quand un dispatch dit ce qu'il écrit, cela
sépare toujours les passes comme avant.

Chaque passe retient : intervalle d'événements, cibles écrites, ressources lues, shaders utilisés,
nombre de draws, timing agrégé.

## Étape 2 — graphe de dépendances des ressources

Pour chaque ressource, à partir des champs `primary_resource` et `secondary_resource` de chaque
événement. Un point non évident : pour un **draw**, la ressource secondaire est la cible de
profondeur, et un depth prepass l'**écrit**. L'API ne dit pas si l'écriture de profondeur est
activée, donc elle compte à la fois comme lecture et comme écriture — c'est ce qui fait apparaître
le prepass comme producteur du depth buffer que la passe d'éclairage consommera. Pour une copie,
la ressource secondaire est la source, donc une lecture seule.

On obtient :

```
Créée par · Écrite par · Lue par · Copiée depuis · Copiée vers · Résolue depuis · Résolue vers · Détruite
```

Chaque arête porte l'index de l'événement qui l'a produite, donc on peut toujours remonter du
graphe à la commande exacte. Les arêtes passe → passe viennent de « la passe B lit une ressource
écrite par la passe A ».

## Étape 3 — classification heuristique

Appliquée sur des invariants, avec un score de confiance :

| Invariant observé | Nom proposé |
|---|---|
| profondeur seule, pas de RT couleur, tôt dans la frame | Depth Prepass |
| ≥ 3 RT simultanés de formats normal / albédo / roughness | GBuffer |
| compute lisant profondeur + normales, écrivant un UAV pleine résolution | Lighting / AO |
| chaîne de RT HDR à résolution divisée par deux successivement | Bloom downsample |
| passe unique lisant un RT HDR, écrivant un RT LDR juste avant l'UI | Tonemap |
| petits draws alpha-blend écrivant le back buffer en fin de frame | UI |

Une passe qui n'écrit que de la profondeur est distinguée d'une shadow map par la **taille** du
depth buffer qu'elle écrit : le depth buffer de la caméra fait la taille de ce qui est présenté,
une shadow map fait une autre taille. Cela remplace « est-ce tôt dans la frame ? », qui qualifiait
chaque cascade d'ombre d'un vrai jeu de depth prepass. Quand rien n'est encore présenté, et
seulement dans ce cas, la position dans la frame est réutilisée, avec une confiance bien moindre.

Tout le reste devient `Unknown Pass #N`. L'origine du nom (`user`, `heuristic`, `ai`) et sa
confiance sont **toujours affichées**, et la couleur du nom dans l'interface suit la confiance :
une inférence n'est jamais présentée comme un fait (§25, §40). L'utilisateur peut renommer une
passe ; son nom survit aux reconstructions et passe en origine `user`.

## Limites actuelles

* Un **dispatch ne dit pas ce qu'il écrit** : sans suivi des descripteurs, les passes compute sont
  regroupées par les barrières qui les séparent et leurs dépendances restent partielles. La capture
  profonde, elle, suit les descripteurs et comble ce manque — voir [CaptureModes.md](CaptureModes.md).
* Une passe que rien ne reconnaît est nommée `Unknown Pass #N (LxH)`. La taille est un fait, pas
  une supposition, et c'est ce qui distingue une passe anonyme d'une autre dans une liste : une
  échelle de post-traitement se lit 1708x960, puis 854x480, et ainsi de suite. L'origine reste
  `unknown` et la confiance zéro.
* Seuls **quatre render targets** par binding sont transportés (les slots 0 à 3).
* La classification ne regarde qu'une frame : elle ne profite pas encore de la stabilité d'une
  passe d'une frame à l'autre.

## Rendu

Vue en colonne navigable (Depth → GBuffer → Lighting → SSR → Volumetrics → Bloom → Tonemap → UI →
Present), avec sélection croisée : cliquer une passe filtre les événements, les shaders et les
ressources concernés.
