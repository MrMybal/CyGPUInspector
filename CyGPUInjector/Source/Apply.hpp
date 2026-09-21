// CyGPUInjector — applying the loaded packages through the ReShade add-on API.
//
// Two mechanisms, both of them plain uses of the official API:
//
//   replace   `create_pipeline` hands an add-on the description the game is about to create a
//             pipeline from, and lets it change it. The byte code from the package is written in
//             before the graphics API ever sees the original. This is the simple path, and it is
//             available here precisely because the modifications are known up front — the
//             inspector has to do something far more involved, since it only learns which shader
//             it wants to replace long after the pipeline exists.
//
//   disable   a draw or dispatch callback returns `true` to say the command was handled, which
//             makes ReShade drop it. Whatever that shader drew simply is not there.
//
// Nothing here hooks, proxies or injects anything: ReShade does the loading, this add-on only
// answers its callbacks.
//
// Copyright (C) 2026 Cyberalien. Licensed under the GNU AGPL v3 or later.
#pragma once

namespace cygi
{
	void RegisterModEvents();
	void UnregisterModEvents();
}
