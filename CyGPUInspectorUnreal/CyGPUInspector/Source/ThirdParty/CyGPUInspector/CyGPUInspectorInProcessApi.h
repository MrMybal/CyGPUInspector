/* CyGPUInspector — the in-process interface of CyGPUInspectorRS.
 *
 * A few plain C functions exported by CyGPUInspectorRS.addon64, for code that runs in the same
 * process as the add-on and wants to drive it without going through the standalone: the Unreal
 * plugin, which asks for a capture from an editor button or a console command.
 *
 * Plain C on purpose: this header is copied as is into the Unreal plugin, which is built by
 * Unreal's own tool chain and must not depend on anything else of this repository. Every struct
 * starts with its own size, so either side can be newer than the other: a caller fills `size`
 * with sizeof of the struct it knows, the add-on reads and writes no further than that.
 *
 * Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
 */
#ifndef CYGPUINSPECTOR_INPROCESS_API_H
#define CYGPUINSPECTOR_INPROCESS_API_H

#include <stdint.h>

#define CYGI_INPROCESS_API_VERSION 2u

#ifdef __cplusplus
extern "C" {
#endif

/* What a capture requested from inside the process records. The same choices as the Deep capture
 * panel of the standalone. */
typedef struct CygiCaptureOptions
{
	uint32_t size;               /* sizeof(CygiCaptureOptions) */
	uint32_t frame_count;        /* 1 to 8 */
	uint32_t include_bindings;   /* every descriptor of every command */
	uint32_t include_barriers;   /* resource transitions */
	uint32_t per_draw_timing;    /* a GPU timestamp per command rather than per pass */
	uint32_t include_buffers;    /* copy the textures the last frame wrote to, for the standalone to save */
} CygiCaptureOptions;

/* Where the add-on is, as of its last present. */
typedef struct CygiStatus
{
	uint32_t size;                    /* sizeof(CygiStatus) */
	uint32_t api_version;             /* CYGI_INPROCESS_API_VERSION of the add-on */
	uint32_t device_count;            /* graphics devices the add-on is attached to */
	uint32_t graphics_api;            /* GraphicsApi of the first one: 0xc000 is Direct3D 12 */
	uint32_t tracking_level;          /* TrackingLevel: 0 idle, 1 tracking, 2 pass timing, ... */
	uint32_t standalone_connected;    /* a CyGPUInspectorApp is connected and will save captures */
	uint32_t standalone_process_id;
	uint32_t capture_stage;           /* CaptureStage: 0 idle, 1 armed, 2 capturing, 3 finished, 4 failed */
	uint64_t capture_first_frame;     /* identifies the last capture */
	uint32_t capture_frames_done;
	uint32_t capture_frames_requested;
	uint64_t frame_index;             /* presents seen so far */
	uint32_t pending_request;         /* a request is queued and not armed yet */
	uint32_t viewport_scope;          /* version 2: a viewport scope is set (see below) */
} CygiStatus;

/* Version 2. The part of the frame a capture is about, when the host knows it. The Unreal plugin
 * clears two 1x1 textures of its own right before and right after the 3D render of the main
 * viewport, and names the texture that render ends in. A capture then records only the commands
 * between the two clears, waits for a frame that has them, and takes that texture as the final
 * image rather than the whole window. Native handles: ID3D12Resource* or ID3D11Resource*; all
 * zero clears the scope. */
typedef struct CygiViewportScope
{
	uint32_t size;               /* sizeof(CygiViewportScope) */
	uint32_t reserved;
	uint64_t begin_marker;
	uint64_t end_marker;
	uint64_t final_image;
} CygiViewportScope;

/* Returns 1 when the request is queued; it is armed at the next present of the first device, like
 * a request from the standalone. Returns 0 when there is no device yet or options is malformed. */
typedef int32_t (*PFN_CyGPUInspectorRS_RequestCapture)(const CygiCaptureOptions *options);
/* Fills as much of *status as status->size allows and returns 1, or 0 when status is null. */
typedef int32_t (*PFN_CyGPUInspectorRS_GetStatus)(CygiStatus *status);
typedef uint32_t (*PFN_CyGPUInspectorRS_GetApiVersion)(void);
/* Version 2. Returns 1, or 0 when scope is null or malformed. */
typedef int32_t (*PFN_CyGPUInspectorRS_SetViewportScope)(const CygiViewportScope *scope);

#define CYGI_EXPORT_REQUEST_CAPTURE "CyGPUInspectorRS_RequestCapture"
#define CYGI_EXPORT_GET_STATUS "CyGPUInspectorRS_GetStatus"
#define CYGI_EXPORT_GET_API_VERSION "CyGPUInspectorRS_GetApiVersion"
#define CYGI_EXPORT_SET_VIEWPORT_SCOPE "CyGPUInspectorRS_SetViewportScope"

#ifdef __cplusplus
}
#endif

#endif
