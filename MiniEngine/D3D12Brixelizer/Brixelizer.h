#pragma once

#include "pch.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "GameInput.h"
#include "CommandContext.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "BufferManager.h"
#include "CameraController.h"
#include "Camera.h"
#include "Model.h"
#include "ModelLoader.h"
#include "ShadowCamera.h"
#include "TemporalEffects.h"
#include "MotionBlur.h"
#include "DepthOfField.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "FXAA.h"
#include "ParticleEffectManager.h"
#include "SponzaRenderer.h"
#include "glTF.h"
#include "Renderer.h"
#include "Display.h"
#include "D3DCompiler.h"
#include "DXSampleHelper.h"

struct Vertex
{
	XMFLOAT3 Vertice; // Position
};