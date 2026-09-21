# IPC — CyGPUInspectorRS ↔ CyGPUInspectorApp

*Disponible aussi en [anglais](../IPC.md), qui fait foi.*

**État : implémenté et testé** (`Tests/CyGPUInspectorCoreTests`, `Tests/CyGPUInspectorIpcTests`).

Trois mécanismes séparés, parce que les trois besoins n'ont pas les mêmes contraintes.

## 1. Annuaire des sessions — découverte

Windows ne sait pas énumérer les objets noyau nommés. Chaque instance de l'add-on publie donc son
existence dans un file mapping partagé, `Local\CyGPUInspector.Sessions.v1`, créé par le premier
arrivé (add-on ou standalone, peu importe) :

```
SessionDirectoryHeader { magic, version, slot_count }
SessionSlot[32] { process_id (atomique), device_index, api, protocol_version, capability_flags,
                  level, start_time_ms, heartbeat_ms (atomique), frame_index (atomique),
                  process_name[64], ring_name[96], signal_name[96], pipe_name[96], ring_capacity }
```

* Un slot est réclamé par `compare_exchange` sur `process_id`, ce qui rend la prise de slot sûre
  entre processus sans verrou.
* `heartbeat_ms` est mis à jour à chaque `present`. Un slot est considéré périmé après 3 secondes.
* Avant de lister un slot, le standalone vérifie que le processus existe encore
  (`OpenProcess` + `GetExitCodeProcess`) : un jeu qui a crashé ne laisse pas de fantôme.
* 32 slots : plusieurs jeux et plusieurs devices par jeu peuvent coexister (§65 du cahier des charges).

## 2. Ring buffer d'événements — le flux de données

`Local\CyGPUInspectorRS.<pid>.<device>.events`, 64 Mio par défaut, un producteur, un consommateur.

```
RingHeader (128 octets, aligné cache) { magic, version, capacity, write_pos, read_pos,
                                        dropped_bytes, dropped_records, sequence,
                                        writer_alive, reader_attached }
Record { RecordHeader{ size, type, flags, sequence } + charge utile }
```

Décisions importantes :

* **Records alignés sur 16 octets.** Quand un record ne tient pas jusqu'à la fin du tampon, un
  record `padding` est écrit jusqu'au bout et le record réel repart du début. Un record n'est donc
  jamais coupé en deux, et le lecteur peut pointer directement dans le mapping, sans recopie. Le
  choix de 16 (plutôt que 8) garantit qu'un record de bourrage est toujours au moins aussi grand
  qu'un `RecordHeader`, sinon le lecteur ne pourrait pas lire l'en-tête sur lequel il tombe.
* **Le producteur ne bloque jamais.** Si le ring est plein, le record est abandonné et
  `dropped_bytes` / `dropped_records` sont incrémentés. L'add-on ne fait jamais attendre le jeu
  parce que le standalone est lent ; la perte est remontée dans `FrameEndRecord::dropped_events` et
  affichée dans l'UI.
* **Le lecteur valide tout.** `size` est vérifié contre l'espace disponible, la capacité et
  l'alignement avant tout déréférencement : si le jeu est tué en plein milieu d'une écriture, le
  lecteur saute au point d'écriture courant au lieu de lire de la mémoire arbitraire.
* Un `Event` Win32 auto-reset (`…​.signal`) est déclenché une fois par frame, pas une fois par
  record : le thread lecteur dort au lieu de tourner.

### Records

`session_info`, `shader_code` (+ bytecode), `pipeline_info` (+ ids de shaders), `resource_info`,
`resource_named`, `resource_gone`, `frame_begin`, `frame_events` (+ `FrameEvent[]`), `frame_end`,
`timing_results`, `preview_ready`, `log_message`, `stats`.

Les événements d'une frame sont découpés en blocs de 4096 (196 Kio) pour ne jamais demander un
trou contigu énorme dans le ring.

## 3. Canal de contrôle — les commandes

Named pipe en mode message, `\\.\pipe\CyGPUInspector\<pid>.<device>`, un seul client.

* Le serveur (add-on) **ne traite jamais une commande sur le thread du pipe** : les messages sont
  mis en file et consommés par le thread de rendu dans `present`. Les appels à l'API ReShade et au
  GPU restent ainsi sur le thread qui possède le device.
* Le serveur et le client utilisent des **entrées/sorties overlapped avec un événement d'arrêt
  explicite**. C'est nécessaire, pas décoratif : un pipe bloquant ne peut pas être arrêté de façon
  fiable (`ConnectNamedPipe` et `ReadFile` peuvent rester parqués indéfiniment, et réveiller le
  thread avec une connexion factice est une course avec le ré-armement du pipe). Dans un jeu, cela
  se traduirait par un blocage au déchargement de ReShade.
* `HelloRequest` transmet le PID du standalone : c'est ce qui permettra plus tard de dupliquer les
  handles NT des textures partagées vers son processus (voir [GPUSharing.md](GPUSharing.md)).

## 4. Ce que garantissent les tests

`CyGPUInspectorIpcTests` lance `CyGPUInspectorFakeSession` (un add-on synthétique, sans jeu ni GPU),
le découvre par l'annuaire, s'y connecte avec le vrai code du standalone et vérifie :

* shaders, pipelines et ressources arrivent complets, bytecode compris ;
* une frame de 2047 événements arrive avec des index **contigus** — c'est ce qui prouve que ni le
  découpage en blocs ni le bouclage du ring ne perd ou ne duplique quoi que ce soit ;
* la corrélation shader → draw calls est correcte (1200 draws pour le shader GBuffer) ;
* une commande `disable_shader` est acquittée et les draws reviennent marqués `skipped`.
