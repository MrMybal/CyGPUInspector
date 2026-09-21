# Profiling GPU

*Disponible aussi en [anglais](../GPUProfiling.md), qui fait foi.*

**État : implémenté et testé** (milestone 8).

## Mécanisme

L'API ReShade expose tout le nécessaire :

* `device::create_query_heap(query_type::timestamp, count, &heap)` ;
* `command_list::end_query(heap, query_type::timestamp, index)` autour d'une portion de frame ;
* `command_list::copy_query_heap_results(...)` vers un tampon de relecture qui nous appartient ;
* `command_queue::signal(fence, value)` et `device::get_completed_fence_value(fence)` pour savoir
  quand cette copie a réellement été exécutée ;
* `device::map_buffer_region(...)` pour la lire ;
* `command_queue::get_timestamp_frequency()` pour convertir les ticks en millisecondes.

La disponibilité est détectée en créant un heap de test à l'initialisation du device, parce que
`device_caps` ne l'annonce pas.

### Pourquoi pas `get_query_heap_results`

C'est la fonction évidente, et elle ne fonctionne pas ici. En D3D12, ReShade tient une fence par
requête et renvoie `false` tant que cette fence n'a pas été signalée — mais il ne signale que les
fences des requêtes émises par son propre runtime. Les requêtes qu'un add-on enregistre sur les
command lists **du jeu** n'ont aucune fence derrière elles, donc l'appel renvoie `false` pour
toujours. Le premier essai de ce code sur un vrai jeu D3D12 a résolu **zéro** frame sur plusieurs
centaines pendant que les query heaps se remplissaient, et la timeline annonçait « pas de timings
GPU » sans moyen de savoir pourquoi.

`copy_query_heap_results` n'a pas cette condition : c'est un `ResolveQueryData` vers un tampon qui
nous appartient, donc la synchronisation devient la nôtre, et c'est à cela que sert la fence
ci-dessus. Tout cela reste l'API add-on officielle, et rien de l'usage que le jeu fait de D3D12
n'est intercepté ni remplacé.

Une fréquence nulle n'est jamais retenue non plus. Un jeu D3D12 crée plusieurs queues, et ce code
s'exécute une fois par queue ; une queue de copie qui n'annonce aucune fréquence de timestamp ne
doit pas écraser celle de la queue graphique, sans quoi chaque tick devient inconvertible et la
timeline n'affiche de nouveau rien.

## La contrainte qui décide de tout

ReShade appelle un add-on **avant** une commande, jamais après. Un draw ne peut donc pas être
encadré par deux timestamps. Ce qui est mesuré est l'**intervalle entre une commande enregistrée
et la suivante** : le temps attribué à un draw est le temps écoulé de son début au début de la
commande enregistrée suivante. C'est ce que tout intercepteur « avant seulement » peut mesurer
honnêtement, et l'interface le dit au lieu de laisser croire à une mesure exacte par draw.

Conséquence pratique : un seul timestamp par point de mesure, pas deux. Le nombre de requêtes est
divisé par deux et le coût GPU avec.

## Quelle commande un timestamp mesure

Un timestamp est pris pendant l'**enregistrement** d'une command list, et à ce moment le seul
numéro qu'ait la commande est sa position dans cette liste. Quelle commande de la **frame** c'est
n'existe qu'une fois la liste exécutée et ses événements fusionnés dans la frame. Une marque est
donc remise sans attribution, la liste la garde, et quand la liste est fusionnée la marque apprend
où la liste a atterri (`GpuTimer::Attribute`). Une liste réinitialisée sans avoir été exécutée ne
s'est jamais exécutée, et ses marques continuent de ne rien mesurer.

C'était faux dans la première version et D3D11 le masquait : là, l'unique contexte immédiat numérote
ses commandes comme la frame. En D3D12 chaque command list numérote à partir de zéro, et toutes les
mesures d'une frame tombaient sur ses premières commandes — sur Scorn, 66 mesures sur les
événements 1 à 83 d'une frame de 200 commandes.

## Ordre d'exécution, et où commence chaque mesure

Le heap est rempli dans l'ordre d'**enregistrement**, à travers tous les threads sur lesquels le
jeu enregistre ; le GPU exécute les listes dans l'ordre d'**exécution**. Les timestamps sont donc
triés par temps avant toute mesure, et chacun dure jusqu'au point suivant que le GPU a réellement
atteint. Chaque résultat porte aussi `gpu_start`, sa position depuis le premier point mesuré de la
frame : c'est ce qui permet à la vue de timing de placer une passe là où elle s'est exécutée plutôt
que bout à bout.

Une valeur que le GPU n'a pas écrite cette frame — une requête dont la liste n'a jamais été
exécutée garde ce que le slot contenait quatre frames plus tôt — est plus ancienne que le timestamp
de fermeture de la frame précédente, et elle est jetée au lieu de devenir une durée énorme. La
ligne de journal les compte comme `stale discarded`.

`gpu_start` a été ajouté sans casser le format : `TimingResultsRecord` dit combien d'octets prend
chaque résultat (le champ s'appelait `reserved` et valait zéro), donc une application lit les
résultats de seize octets d'un add-on plus ancien comme des résultats sans position, et le dit dans
la vue.

## Lecture différée

Quatre heaps en anneau, chacun avec son propre tampon de relecture, résultats lus pour la frame
N-3. On n'attend **jamais** le GPU : une copie dont la fence n'est pas encore passée est simplement
retentée la frame suivante. Les résultats transportent donc **leur propre numéro de frame**,
affiché dans l'interface, plutôt que d'être attribués à la frame affichée.

Un slot est abandonné au bout de quatre tentatives. Cela compte plus qu'il n'y paraît : un slot
jamais libéré retient aussi ses requêtes, et dès que les quatre sont retenus le heap est plein,
toute marque ultérieure est jetée et le chronomètre ne se rétablit plus de toute la session.
L'add-on journalise, une fois par seconde tant qu'un niveau de timing est actif, combien de frames
ont été résolues, combien ont été abandonnées et combien de marques ont été jetées : une panne de
ce genre est donc visible dans `ReShade.log` au lieu d'être une colonne vide dans l'interface.

## Niveaux

| Niveau | Ce qui est instrumenté | État |
|---|---|---|
| `Tracking` et en dessous | rien | — |
| `Pass Timing` | un timestamp à chaque changement de cible de rendu | **implémenté** |
| `Full Draw Timing` | un timestamp par draw et par dispatch | **implémenté** |
| `Selected Shader` / `Selected Draw` | filtrage côté add-on | à venir |

L'agrégation par shader et par passe est faite **côté standalone**, à partir des mesures par
commande : c'est gratuit et cela évite d'embarquer la logique d'agrégation dans le jeu.

`Full Draw Timing` peut diviser le framerate par deux. C'est un mode diagnostic, activé
explicitement, et annoncé comme tel dans l'interface (§23, §67).

## Restitution

* par passe : `Depth Prepass 0.42 ms · GBuffer 1.31 ms · Lighting 2.82 ms …` ;
* par shader : appels, total GPU, moyenne, maximum ;
* par draw : la valeur brute, dans l'inspecteur d'événement.

Le nombre de timestamps par frame est plafonné (`MaxTimestamps`, 16384 par défaut) ; au-delà, les
marques sont comptées comme abandonnées plutôt que d'agrandir le heap en plein rendu.

## Limites

* Le temps d'une commande inclut ce qui se passe entre elle et la commande enregistrée suivante
  (changements d'état, commandes non suivies).
* En D3D12 avec plusieurs command lists, les intervalles de listes différentes ne sont pas
  comparables directement : les timestamps sont sur la même queue mais l'ordre d'enregistrement
  n'est pas l'ordre d'exécution.
* Les résultats viennent de la frame N-3 et sont rapprochés de la frame courante par index
  d'événement. D'une frame à l'autre le contenu est presque identique, mais c'est une
  approximation — le numéro de frame mesuré est affiché pour cette raison.

## Mesure du coût de l'outil

Indépendamment du profiling du jeu, l'add-on mesure déjà son propre temps CPU par frame
(`FrameEndRecord::addon_cpu_ms`) et son débit IPC (`StatsRecord`). Le §68 demande d'étendre cela à
la VRAM des textures partagées et à la comparaison entre modes ; les compteurs sont en place.
