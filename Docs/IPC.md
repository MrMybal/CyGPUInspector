# IPC — CyGPUInspectorRS ↔ CyGPUInspectorApp

*Also available in [French](fr/IPC.md).*

**State: implemented and tested** (`Tests/CyGPUInspectorCoreTests`, `Tests/CyGPUInspectorIpcTests`).

Three separate mechanisms, because the three needs do not have the same constraints.

## 1. Session directory — discovery

Windows cannot enumerate named kernel objects. Each instance of the add-on therefore publishes its
existence in a shared file mapping, `Local\CyGPUInspector.Sessions.v1`, created by whoever gets
there first — add-on or standalone, it does not matter:

```
SessionDirectoryHeader { magic, version, slot_count }
SessionSlot[32] { process_id (atomic), device_index, api, protocol_version, capability_flags,
                  level, start_time_ms, heartbeat_ms (atomic), frame_index (atomic),
                  process_name[64], ring_name[96], signal_name[96], pipe_name[96], ring_capacity }
```

* A slot is claimed with a `compare_exchange` on `process_id`, which makes claiming one safe
  between processes without a lock.
* `heartbeat_ms` is updated on every `present`. A slot is considered stale after 3 seconds.
* Before listing a slot, the standalone checks that the process still exists (`OpenProcess` +
  `GetExitCodeProcess`): a game that crashed leaves no ghost behind.
* 32 slots: several games, and several devices per game, can coexist (§65 of the brief).

## 2. Event ring buffer — the data stream

`Local\CyGPUInspectorRS.<pid>.<device>.events`, 64 MiB by default, one producer, one consumer.

```
RingHeader (128 bytes, cache aligned) { magic, version, capacity, write_pos, read_pos,
                                        dropped_bytes, dropped_records, sequence,
                                        writer_alive, reader_attached }
Record { RecordHeader{ size, type, flags, sequence } + payload }
```

Decisions that matter:

* **Records are 16-byte aligned.** When a record does not fit before the end of the buffer, a
  `padding` record is written to the end and the real record starts again at the beginning. A
  record is therefore never cut in two, and the reader can point straight into the mapping with no
  copy. Choosing 16 rather than 8 guarantees that a padding record is always at least as large as
  a `RecordHeader`, without which the reader could not read the header it lands on.
* **The producer never blocks.** If the ring is full the record is dropped and `dropped_bytes` /
  `dropped_records` are incremented. The add-on never makes the game wait because the standalone is
  slow; the loss is reported in `FrameEndRecord::dropped_events` and shown in the interface.
* **The reader validates everything.** `size` is checked against the available space, the capacity
  and the alignment before anything is dereferenced: if the game is killed halfway through a write,
  the reader skips to the current write position instead of reading arbitrary memory.
* An auto-reset Win32 `Event` (`….signal`) is raised once per frame, not once per record: the
  reader thread sleeps instead of spinning.

### Records

`session_info`, `shader_code` (+ byte code), `pipeline_info` (+ shader ids), `resource_info`,
`resource_named`, `resource_gone`, `frame_begin`, `frame_events` (+ `FrameEvent[]`), `frame_end`,
`timing_results`, `preview_ready`, `log_message`, `stats`, and, for the deep capture only,
`draw_state` (+ `DrawBinding[]`), `barrier_set` (+ `BarrierEntry[]`), `pipeline_state`,
`capture_state`.

The events of a frame are chunked into blocks of 4096 (196 KiB) so the ring is never asked for one
enormous contiguous hole.

## 3. Control channel — the commands

A message-mode named pipe, `\\.\pipe\CyGPUInspector\<pid>.<device>`, one client.

* The server (the add-on) **never handles a command on the pipe thread**: messages are queued and
  consumed by the rendering thread inside `present`. Calls into the ReShade API and into the GPU
  therefore stay on the thread that owns the device.
* Server and client both use **overlapped I/O with an explicit stop event**. That is necessary,
  not decorative: a blocking pipe cannot be shut down reliably (`ConnectNamedPipe` and `ReadFile`
  can park indefinitely, and waking the thread with a dummy connection races the pipe being
  re-armed). Inside a game that would show up as a freeze when ReShade unloads.
* `HelloRequest` carries the standalone's PID, which is what lets NT handles for shared textures be
  duplicated into its process (see [GPUSharing.md](GPUSharing.md)).

## 4. What the tests guarantee

`CyGPUInspectorIpcTests` starts `CyGPUInspectorFakeSession` (a synthetic add-on, with no game and
no GPU), discovers it through the directory, connects with the standalone's real code, and checks:

* shaders, pipelines and resources arrive complete, byte code included;
* a frame of 2047 events arrives with **contiguous** indices — which is what proves that neither
  the chunking nor the ring wrapping loses or duplicates anything;
* the shader → draw call correlation is right (1200 draws for the GBuffer shader);
* a `disable_shader` command is acknowledged and the draws come back marked `skipped`;
* a deep capture arms, delivers per command state whose event indices resolve, and disarms itself.
