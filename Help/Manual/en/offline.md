# Save, reopen, compare and share captures

## Save a frame
With a live frame available, use Capture frame, File > Save frame to disk or Ctrl+S outside a focused text editor. The capture contains one frame, its events and the bytecode of the shaders it used. It is a directory with a versioned manifest and binary payloads, not a video or a GPU replay.

## Reopen offline
Open Captures, refresh the list if needed, then open a saved entry. The status changes to OFFLINE CAPTURE. Inspect its shaders, command stream, derived graph and available analysis without the game. Live operations and texture previews are unavailable. Return to the live session when you need to apply a change.

## Compare
Choose the two saved captures in the Captures comparison controls. Matching uses shader signatures rather than session-local identifiers. Use the differences to identify changed or new shaders and activity, then open each capture to verify the underlying evidence. Changed capture conditions can explain a difference; it does not automatically imply a regression.

## Share a reproducible case
Copy the entire capture directory, not just capture.json. Include separately exported deep buffers when pixels are relevant. An AI analysis snapshot adds the already computed disassembly, decompiler reconstructions and a readable summary to a full saved frame. It does not run a remote model or finish missing analysis by itself.

Inspect the data before sending it: shader code, resource names and inferred structure can be part of the case. The synthetic demo is preferable for public screenshots. Keep a note of application version, capture mode, API and the exact question you want reproduced.

If an older/newer capture version is refused, keep the original directory and use a compatible application version; do not edit a version number to force parsing. See [the complete standalone reference](help:ref-StandaloneApp) and [capture limits](help:ref-Limitations).
