# Tracking levels and the capture modes

## Start with runtime observation
Connect a live session and select the lowest tracking level that answers your question. Inventories and command tracking supply progressively more data. Pass Timing adds measurements per pass; Draw Timing is more expensive. At a level without timestamps, bars can represent command order/count: read the ruler instead of treating every width as milliseconds.

## Choose your output
| Need | Use | What remains |
| --- | --- | --- |
| Watch recent performance | Runtime / Frame timeline | Bounded live history |
| Resume analysis later | Save frame to disk | One offline frame and its used shader bytecode |
| Inspect actual bindings and buffers | Deep capture | Bounded command state plus separately exported PNG/DDS files |
| Give another analyst a prepared case | AI analysis snapshot | A saved frame and available analysis files |

## Capture a deep frame
1. Connect to the intended live producer and open Deep capture.
2. Arm the capture with the panel’s action. Recording starts at a frame boundary and stops after the requested bounded interval.
3. Wait for completion and inspect the reported range, command/descriptors counts and incomplete/dropped markers.
4. Select a command to inspect bindings, pipeline fields and barriers.
5. Wait for exported buffers to finish before copying the capture directory. The list reports files not copied or failed.

Final image and buffers are exported as viewable PNG and data-preserving DDS where supported, with capture.json describing them. A regular saved offline frame does not contain these pixels. The [capture diagram](figure:0) distinguishes the three paths.

## What it cannot do
No mode replays rendering commands, gives pixel history, steps through a shader or recovers original HLSL. Deep capture has bounded command/descriptor capacity and can miss state set before arming. A partial capture says so; unknown is not the same as empty. See [capture internals and exact bounds](help:ref-CaptureModes) and [limitations](help:ref-Limitations).
