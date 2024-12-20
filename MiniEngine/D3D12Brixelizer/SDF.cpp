#include "SDF.h"

#include "GraphicsCore.h"
#include "CommandContext.h"

namespace SDF
{
	//BoolVar Enable("Graphics/SDF/Enable", true);
	BoolVar DebugDraw("Graphics/SDF/Debug Draw", true);

	void Initialize()
	{
	}

	void Render(GraphicsContext& Context)
	{
		if (DebugDraw)
		{
			/*
			Context.SetRootSignature(s_RootSignature);
			//Context.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			//Context.TransitionResource(LinearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			Context.SetDynamicDescriptors(2, 0, 1, &g_SceneColorBuffer.GetUAV());
			Context.SetDynamicDescriptors(3, 0, 1, &LinearDepth.GetSRV());
			Context.SetPipelineState(s_DebugSSAOCS);
			Context.Dispatch2D(g_SSAOFullScreen.GetWidth(), g_SSAOFullScreen.GetHeight());
			*/
		}
	}

	void DrawDebug()
	{
		// Create pass to show a sdf
		/*
		*/
	}
};