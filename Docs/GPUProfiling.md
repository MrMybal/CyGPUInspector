# GPU profiling

*Also available in [French](fr/GPUProfiling.md).*

**State: implemented and tested** (milestone 8).

## Mechanism

The ReShade API exposes everything needed:

* `device::create_query_heap(query_type::timestamp, count, &heap)`;
* `command_list::end_query(heap, query_type::timestamp, index)` around a part of a frame;
* `command_list::copy_query_heap_results(...)` into a readback buffer of our own;
* `command_queue::signal(fence, value)` and `device::get_completed_fence_value(fence)` to know when
  that copy has actually been executed;
* `device::map_buffer_region(...)` to read it;
* `command_queue::get_timestamp_frequency()` to turn ticks into milliseconds.

Availability is detected by creating a test heap when the device is initialised, because
`device_caps` does not announce it.

### Why not `get_query_heap_results`

That function is the obvious one, and it does not work here. On D3D12, ReShade keeps one fence per
query and returns `false` until that fence has been signalled — but it only signals the fences of
queries its own runtime issued. Queries an add-on records on the **game's** command lists have no
fence signalled behind them, so the call returns `false` for ever. The first run of this code
against a real D3D12 game resolved **zero** frames out of several hundred while the query heaps
filled up, and the timeline reported "no GPU timings" with no way to tell why.

`copy_query_heap_results` has no such condition: it is `ResolveQueryData` into a buffer we own, so
the synchronisation becomes ours to do, which is what the fence above is for. Everything here is
still the official add-on API, and nothing about the game's own use of D3D12 is intercepted or
replaced.

A frequency of zero is never taken either. A D3D12 game creates several queues, and this runs once
per queue; a copy queue that reports no timestamp frequency must not overwrite the graphics queue's,
or every tick becomes unconvertible and the timeline again reports nothing.

## The constraint that decides everything

ReShade calls an add-on **before** a command, never after. A draw therefore cannot be bracketed by
two timestamps. What is measured is the **interval between one recorded command and the next**: the
time attributed to a draw is the time from its start to the start of the next recorded command.
That is what any "before only" interceptor can honestly measure, and the interface says so rather
than implying an exact per-draw measurement.

The practical consequence: one timestamp per measurement point, not two. That halves the number of
queries and the GPU cost with it.

## Which command a timestamp measures

A timestamp is taken while a command list is being **recorded**, and at that moment the only
number the command has is its position in that list. Which command of the **frame** it is only
exists once the list is executed and its events are merged into the frame. So a mark is handed
out unattributed, the list keeps it, and when the list is merged the mark is told where the list
landed (`GpuTimer::Attribute`). A list reset without being executed never ran, and its marks keep
measuring nothing.

This was wrong in the first version and D3D11 hid it: there, the one immediate context numbers its
commands like the frame does. On D3D12 every command list numbers from zero, and every measurement
of a frame landed on its first few commands — on Scorn, 66 measurements on events 1 to 83 of a
200-command frame.

## Execution order, and where each measurement starts

The heap is filled in **recording** order, across every thread the game records on; the GPU runs
the lists in **execution** order. So the timestamps are sorted by time before anything is measured,
and each one lasts until the next point the GPU actually reached. Each result also carries
`gpu_start`, its position since the frame's earliest measured point, which is what lets the timing
view place a pass where it ran rather than end to end.

A value the GPU never wrote this frame — a query whose list was never executed keeps whatever the
slot held four frames ago — is older than the previous frame's closing timestamp, and is thrown
away rather than turned into a huge duration. The log line counts these as `stale discarded`.

`gpu_start` was added without breaking the wire format: `TimingResultsRecord` says how many bytes
each result takes (the field was `reserved`, and zero), so an application reads an older add-on's
sixteen-byte results as results without positions, and says so in the view.

## Deferred read-back

Four heaps in a ring, each with its own readback buffer, results read for frame N-3. The GPU is
**never** waited on: a copy whose fence has not been passed is simply retried next frame. Results
therefore carry **their own frame number**, shown in the interface, rather than being attributed to
the frame on screen.

A slot is given up after four attempts. This matters more than it sounds: a slot that is never
released holds its queries too, and once all four are held the heap is full, every later mark is
dropped and the timer never recovers for the rest of the run. The add-on logs, once a second while
a timing level is selected, how many frames resolved, how many were abandoned and how many marks
were dropped, so a failure of this kind is visible in `ReShade.log` instead of being an empty
column in the interface.

## Levels

| Level | What is instrumented | State |
|---|---|---|
| `Tracking` and below | nothing | — |
| `Pass Timing` | one timestamp at every render target change | **implemented** |
| `Full Draw Timing` | one timestamp per draw and per dispatch | **implemented** |
| `Selected Shader` / `Selected Draw` | filtering on the add-on side | to come |

Aggregation per shader and per pass is done **on the standalone side**, from the per command
measurements: it is free there and it keeps the aggregation logic out of the game.

`Full Draw Timing` can halve the frame rate. It is a diagnostic mode, turned on explicitly, and
announced as such in the interface (§23, §67). A deep capture turns it on for its own duration and
turns it back off afterwards.

## Presentation

* per pass: `Depth Prepass 0.42 ms · GBuffer 1.31 ms · Lighting 2.82 ms …`, and as time-scaled bars
  in the [frame timeline](CaptureModes.md);
* per shader: calls, GPU total, average, maximum;
* per draw: the raw value, in the event inspector.

The number of timestamps per frame is capped (`MaxTimestamps`, 16384 by default); beyond it, marks
are counted as dropped rather than growing the heap in the middle of rendering.

## Limits

* The time for a command includes whatever happens between it and the next recorded command (state
  changes, commands that are not tracked).
* On D3D12 with several command lists, intervals from different lists are not directly comparable:
  the timestamps are on the same queue but recording order is not execution order.
* Results come from frame N-3 and are matched against the current frame by event index. From one
  frame to the next the content is nearly identical, but it is an approximation — which is why the
  measured frame number is displayed.

## Measuring the cost of the tool

Independently of profiling the game, the add-on already measures its own CPU time per frame
(`FrameEndRecord::addon_cpu_ms`) and its IPC throughput (`StatsRecord`). §68 asks for that to be
extended to shared texture VRAM and to a comparison between modes; the counters are in place.
