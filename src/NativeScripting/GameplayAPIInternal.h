#pragma once

class Renderer;

namespace GameplayAPI
{
	/// Called by the engine to provide the renderer pointer to the gameplay API.
	/// Not exported — only for engine-internal use.
	void setRendererInternal(Renderer* renderer);
}
