// CyGPUInspector — Unreal plugin. Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.

#pragma once

#include "CoreMinimal.h"

#if CYGPUINSPECTOR_ENABLED

#include "RenderGraphResources.h"
#include "SceneViewExtension.h"

// Marks the 3D render of the main viewport for the add-on.
//
// Right before that render and right after it, a 1x1 texture of the plugin's own is cleared. The
// two clears are ordinary rendering commands: the add-on sees them go by like any other, through
// ReShade, and a capture keeps only what lies between them — the scene, not the editor's
// interface drawn around it. The extension also names the texture the render ends in (the
// viewport's own render target in the editor), which becomes the final image.
//
// The markers cost two one-pixel clears per frame of that viewport, and are only added while the
// add-on is in the process.
class FCyGPUInspectorViewExtension : public FSceneViewExtensionBase
{
public:
	FCyGPUInspectorViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;

protected:
	// Game thread: only the viewport the plugin calls the main one, and only with the add-on there.
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	void AddMarker(FRDGBuilder& GraphBuilder, TRefCountPtr<IPooledRenderTarget>& Pooled, const TCHAR* Name);
	void PublishScope(const FSceneViewFamily& InViewFamily);

	// Render thread only.
	TRefCountPtr<IPooledRenderTarget> BeginMarker;
	TRefCountPtr<IPooledRenderTarget> EndMarker;
	uint64 PublishedBegin = 0;
	uint64 PublishedEnd = 0;
	uint64 PublishedFinal = 0;
};

#endif
