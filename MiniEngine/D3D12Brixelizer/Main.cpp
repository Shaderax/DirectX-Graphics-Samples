#include "Brixelizer.h"
#include "SDF.h"

using namespace GameCore;
using namespace Graphics;
using namespace Renderer;

ExpVar g_SunLightIntensity("Viewer/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
NumVar g_SunOrientation("Viewer/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f);
NumVar g_SunInclination("Viewer/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f);

class D3D12Brixelizer : public GameCore::IGameApp
{
public:

    D3D12Brixelizer()
    {
    }

    virtual void Startup( void ) override;
    virtual void Cleanup( void ) override;

    virtual void Update( float deltaT ) override;
    virtual void RenderScene( void ) override;

private:
    Camera Camera;
    std::unique_ptr<CameraController> CameraController;

    D3D12_VIEWPORT MainViewport;
    D3D12_RECT MainScissor;

    ModelInstance ModelInst;
    ShadowCamera SunShadowCamera;

    ComPtr<ID3D12Resource> VertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView;

    RootSignature ComputeRootSig;

    // Debug
    ColorBuffer T3d;
    StructuredBuffer buf;
};
   
ComputePSO ComputeCS_PSO(L"Compute CS");

CREATE_APPLICATION( D3D12Brixelizer )

void ChangeIBLSet(EngineVar::ActionType);
void ChangeIBLBias(EngineVar::ActionType);

DynamicEnumVar g_IBLSet("Viewer/Lighting/Environment", ChangeIBLSet);
std::vector<std::pair<TextureRef, TextureRef>> g_IBLTextures;
NumVar g_IBLBias("Viewer/Lighting/Gloss Reduction", 2.0f, 0.0f, 10.0f, 1.0f, ChangeIBLBias);

void ChangeIBLSet(EngineVar::ActionType)
{
    int setIdx = g_IBLSet - 1;
    if (setIdx < 0)
    {
        Renderer::SetIBLTextures(nullptr, nullptr);
    }
    else
    {
        auto texturePair = g_IBLTextures[setIdx];
        Renderer::SetIBLTextures(texturePair.first, texturePair.second);
    }
}

void ChangeIBLBias(EngineVar::ActionType)
{
    Renderer::SetIBLBias(g_IBLBias);
}

#include <direct.h> // for _getcwd() to check data root path

void LoadIBLTextures()
{
    char CWD[256];
    _getcwd(CWD, 256);

    Utility::Printf("Loading IBL environment maps\n");

    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(L"../ModelViewer/Textures/*_diffuseIBL.dds", &ffd);

    g_IBLSet.AddEnum(L"None");

    if (hFind != INVALID_HANDLE_VALUE) do
    {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::wstring diffuseFile = ffd.cFileName;
        std::wstring baseFile = diffuseFile;
        baseFile.resize(baseFile.rfind(L"_diffuseIBL.dds"));
        std::wstring specularFile = baseFile + L"_specularIBL.dds";

        TextureRef diffuseTex = TextureManager::LoadDDSFromFile(L"../ModelViewer/Textures/" + diffuseFile);
        if (diffuseTex.IsValid())
        {
            TextureRef specularTex = TextureManager::LoadDDSFromFile(L"../ModelViewer/Textures/" + specularFile);
            if (specularTex.IsValid())
            {
                g_IBLSet.AddEnum(baseFile);
                g_IBLTextures.push_back(std::make_pair(diffuseTex, specularTex));
            }
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    FindClose(hFind);

    Utility::Printf("Found %u IBL environment map sets\n", g_IBLTextures.size());

    if (g_IBLTextures.size() > 0)
        g_IBLSet.Increment();
}

void D3D12Brixelizer::Startup( void )
{
    MotionBlur::Enable = true;
    TemporalEffects::EnableTAA = true;
    FXAA::Enable = false;
    PostEffects::EnableHDR = true;
    PostEffects::EnableAdaptation = true;
    SSAO::Enable = true;

    Renderer::Initialize();

    LoadIBLTextures();

    Microsoft::WRL::ComPtr<ID3DBlob> bloob = CompileShader(L"Shaders/EmitSDFCS.hlsl", nullptr, "main", "cs_5_1");
    
    ComputeRootSig.Reset(3, 0);
    ComputeRootSig[0].InitAsConstantBuffer(0);
    ComputeRootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
    ComputeRootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
    ComputeRootSig.Finalize(L"FillLightRS");

    ComputeCS_PSO.SetRootSignature(ComputeRootSig);
    ComputeCS_PSO.SetComputeShader(bloob->GetBufferPointer(), bloob->GetBufferSize());
    ComputeCS_PSO.Finalize();

    ModelInst = Renderer::LoadModel(L"../ModelViewer/Sponza/PBR/sponza2.gltf", false);
    ModelInst.Resize(100.0f * ModelInst.GetRadius());
    OrientedBox obb = ModelInst.GetBoundingBox();
    float modelRadius = Length(obb.GetDimensions()) * 0.5f;
    const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
    Camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));

    Camera.SetZRange(1.0f, 10000.0f);
    CameraController.reset(new FlyingFPSCamera(Camera, Vector3(kYUnitVector)));

    // Create T3d
    {
        // Tex 3D -> 2D = 64*64*64=262144  -> sqrt(262144)=512
        T3d.Create(L"SDF", 512, 512, 0, DXGI_FORMAT_R16_FLOAT);
        buf.Create(L"Oui", 1, 12);
    }

    // Compute Dispatch
    {
        ComputeContext& CmpContext = ComputeContext::Begin(L"Compute");

        __declspec(align(16)) struct Vertex
        {
            XMFLOAT3 Position;
            XMFLOAT3 Normal;
        };

        __declspec(align(16)) struct CSConstants
        {
            XMFLOAT3 Center;
            uint32_t NumTriangles;
        } csConstants;
        csConstants.Center = XMFLOAT3(Camera.GetPosition().GetX(), Camera.GetPosition().GetY(), Camera.GetPosition().GetZ());
        csConstants.NumTriangles = ModelInst.GetModel()->m_DataBuffer.GetBufferSize() / (sizeof(Vertex) * 3);

        CmpContext.SetRootSignature(ComputeRootSig);
        CmpContext.SetPipelineState(ComputeCS_PSO);

        CmpContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &csConstants);
        CmpContext.SetDynamicDescriptor(1, 0, ModelInst.GetModel()->m_DataBuffer.GetSRV());
        CmpContext.SetDynamicDescriptor(2, 0, T3d.GetUAV());

        CmpContext.Dispatch(1024, 1, 1);

        CmpContext.Finish();
    }
}

void D3D12Brixelizer::Cleanup( void )
{
    ModelInst = nullptr;
    T3d.Destroy();
    buf.Destroy();

    // Free up resources in an orderly fashion
    Renderer::Shutdown();
}

void D3D12Brixelizer::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    // Update something
    CameraController->Update(deltaT);

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");

    ModelInst.Update(gfxContext, deltaT);

    gfxContext.Finish();

    TemporalEffects::GetJitterOffset(MainViewport.TopLeftX, MainViewport.TopLeftY);

    MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    MainViewport.MinDepth = 0.0f;
    MainViewport.MaxDepth = 1.0f;

    MainScissor.left = 0;
    MainScissor.top = 0;
    MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

}

void D3D12Brixelizer::RenderScene( void )
{
    uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();
    const D3D12_VIEWPORT& viewport = MainViewport;
    const D3D12_RECT& scissor = MainScissor;

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

    // Update global constants
    float costheta = cosf(g_SunOrientation);
    float sintheta = sinf(g_SunOrientation);
    float cosphi = cosf(g_SunInclination * 3.14159f * 0.5f);
    float sinphi = sinf(g_SunInclination * 3.14159f * 0.5f);

    Vector3 SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));
    Vector3 ShadowBounds = Vector3(ModelInst.GetRadius());
    //m_SunShadowCamera.UpdateMatrix(-SunDirection, m_ModelInst.GetCenter(), ShadowBounds,
    SunShadowCamera.UpdateMatrix(-SunDirection, Vector3(0, -500.0f, 0), Vector3(5000, 3000, 3000),
        (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

    GlobalConstants globals;
    globals.ViewProjMatrix = Camera.GetViewProjMatrix();
    globals.SunShadowMatrix = SunShadowCamera.GetShadowMatrix();
    globals.CameraPos = Camera.GetPosition();
    globals.SunDirection = SunDirection;
    globals.SunIntensity = Vector3(Scalar(g_SunLightIntensity));

    // Begin rendering depth
    gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
    gfxContext.ClearDepth(g_SceneDepthBuffer);

    MeshSorter sorter(MeshSorter::kDefault);
    sorter.SetCamera(Camera);
    sorter.SetViewport(viewport);
    sorter.SetScissor(scissor);
    sorter.SetDepthStencilTarget(g_SceneDepthBuffer);
    sorter.AddRenderTarget(g_SceneColorBuffer);

    ModelInst.Render(sorter);

    sorter.Sort();

	{
		ScopedTimer _prof(L"Depth Pre-Pass", gfxContext);
		sorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
	}

	SSAO::Render(gfxContext, Camera);

    // Main Render
	if (!SSAO::DebugDraw)
	{
		ScopedTimer _outerprof(L"Main Render", gfxContext);

		{
			ScopedTimer _prof(L"Sun Shadow Map", gfxContext);

			MeshSorter shadowSorter(MeshSorter::kShadows);
			shadowSorter.SetCamera(SunShadowCamera);
			shadowSorter.SetDepthStencilTarget(g_ShadowBuffer);

			ModelInst.Render(shadowSorter);

			shadowSorter.Sort();
			shadowSorter.RenderMeshes(MeshSorter::kZPass, gfxContext, globals);
		}

		gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(g_SceneColorBuffer);

		{
			ScopedTimer _prof(L"Render Color", gfxContext);

			gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
			gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
			gfxContext.SetViewportAndScissor(viewport, scissor);

			sorter.RenderMeshes(MeshSorter::kOpaque, gfxContext, globals);
		}

		Renderer::DrawSkybox(gfxContext, Camera, viewport, scissor);

		sorter.RenderMeshes(MeshSorter::kTransparent, gfxContext, globals);
	}
	// Some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.  Everything
	// is static in our scene, so we generate velocity from camera motion and the depth buffer.  A velocity buffer
	// is necessary for all temporal effects (and motion blur).
	MotionBlur::GenerateCameraVelocityBuffer(gfxContext, Camera, true);

	TemporalEffects::ResolveImage(gfxContext);

	ParticleEffectManager::Render(gfxContext, Camera, g_SceneColorBuffer, g_SceneDepthBuffer, g_LinearDepth[FrameIndex]);

	// Until I work out how to couple these two, it's "either-or".
	if (DepthOfField::Enable)
		DepthOfField::Render(gfxContext, Camera.GetNearClip(), Camera.GetFarClip());
	else
		MotionBlur::RenderObjectBlur(gfxContext, g_VelocityBuffer);

	gfxContext.Finish();
}
