# Read GPU timings and the continuous timeline

## Inspect one frame
Choose Frame timeline > One frame at Pass Timing or higher. The history strip shows recent CPU frame times and available GPU measurements. The timing rows show the frame, passes and commands that have usable measurements. Click a span, then inspect its duration, related shaders, resources and command list.

A dimmed pass or a value prefixed by ~ is estimated. A missing timestamp is not zero GPU cost. Pass colours describe roles; widths describe the measured/estimated interval indicated by the current ruler. User names, derived names and AI hypotheses keep their provenance.

## Navigate
Wheel zooms around the pointer. Drag pans the time view; the common canvas also supports middle-drag and horizontal navigation. Double-click/Fit restores the useful range. The continuous slider moves through retained history. Selection and panning are distinct: a drag must not select an unrelated command.

## Follow several frames
Choose Continuous to place successive measured frames on the GPU clock. Expand your analysis from a spike in the history strip to that frame’s passes. The picked frame’s detail table carries its frame number and start/length values. A gap can be GPU wait or work outside the measured interval; missing frames are identified separately. Retention is bounded, so old commands may no longer be available even when a compact span remains.

## Use timing focus
The precise shader/draw/resource timing actions request targeted measurements while keeping pass-level observation. Check the focus status and wait for delayed results. Query capacity is finite; dropped marks are reported. Results arrive several frames after submission, and their own frame number matters.

## Interpret intervals
ReShade notifies before a command, so command time is the interval to the next measured command, not a hardware-isolated duration. D3D12 multi-list recording order is not GPU execution order. Shared machine load and instrumentation affect timings; compare repeatable conditions rather than a single number. See [the profiling reference](help:ref-GPUProfiling).
