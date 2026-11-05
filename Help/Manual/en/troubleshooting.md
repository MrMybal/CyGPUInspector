# Troubleshooting and known limits

## No session
Confirm the process is 64-bit, uses a supported D3D11/12 path, has the full add-on-enabled ReShade build and actually loaded CyGPUInspectorRS. Check ReShade.log and refresh Connections. A running standalone alone is not a producer. Use the no-game demo to separate installation issues from application issues.

## No timings or missing events
Choose Pass Timing or a suitable focus/level and wait for delayed results. Check losses and timestamp capacity in Status. A zero/unknown frequency or missing marks must not become fabricated timing. Event buffering is bounded and drops data rather than blocking the game. A graph reconstructed from partial events can be incomplete.

## Black/unavailable preview
Check whether the session is live, the selected resource still exists, sharing uses the same adapter and the format/state is supported. Try a reasonable display range/channel and depth/reverse-Z settings. A normal offline capture has no pixels. Deep export can fail per buffer without invalidating every other file.

## Compilation or replacement refused
Check entry point, stage/profile, resource signatures and compiler availability in Tools. Keep the provided DXC DLL pair together for SM6/DXIL signing. Read the diagnostic and compatibility list. Recompilation success does not prove identical behaviour. Restore is the direct rollback for an applied replacement; editor Undo only changes source text.

## AI / MCP unavailable
Check provider CLI login or API endpoint/model and whether a local server is running. API keys are session-only. For MCP, start the standalone, free the single proxy connection and inspect the host’s permission level/refusal log. Pending analysis needs a later query, not repeated overlapping requests.

## Help missing or partly translated
Restore the complete generated Help folder beside the EXE, then reopen Help/index.html; changing the working directory should not matter. The language selector is independent of application language. Missing French content falls back visibly to English. Missing images show a recovery message instead of preventing article reading.

## Fundamental limits
No frame replay, pixel history, shader stepping or original HLSL recovery. Pass names are inferred/user-provided; descriptor information and multi-list timings have explicit limits. Bounded histories and capture sizes are intentional. Consult [the full limits reference](help:ref-Limitations) before diagnosing unsupported behaviour as data loss.
