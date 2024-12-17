#include "Brixelizer.h"

#define UFBX_NO_INDEX_GENERATION

#include "ufbx.h"

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

    void LoadAssets(ComPtr<ID3D12GraphicsCommandList4>& CommandList);

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
};

CREATE_APPLICATION( D3D12Brixelizer )

void D3D12Brixelizer::Startup( void )
{
    MotionBlur::Enable = false;
    TemporalEffects::EnableTAA = false;
    FXAA::Enable = false;
    PostEffects::EnableHDR = false;
    PostEffects::EnableAdaptation = false;
    SSAO::Enable = false;

    Renderer::Initialize();

    //ModelInst = Renderer::LoadModel(std::wstring(L"Assets/MiniatureNightCity.glb"), false);
    //ModelInst.LoopAllAnimations();
    //ModelInst.Resize(10.0f);
    ModelInst = Renderer::LoadModel(L"../ModelViewer/Sponza/PBR/sponza2.gltf", false);
    ModelInst.Resize(100.0f * ModelInst.GetRadius());
    OrientedBox obb = ModelInst.GetBoundingBox();
    float modelRadius = Length(obb.GetDimensions()) * 0.5f;
    const Vector3 eye = obb.GetCenter() + Vector3(modelRadius * 0.5f, 0.0f, 0.0f);
    Camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));

    Camera.SetZRange(1.0f, 10000.0f);
    CameraController.reset(new FlyingFPSCamera(Camera, Vector3(kYUnitVector)));

    // Setup your data
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Build Acceleration Structures");

    ComPtr<ID3D12GraphicsCommandList4> CommandList;
    gfxContext.GetCommandList()->QueryInterface(IID_PPV_ARGS(&CommandList));

    //LoadAssets(CommandList);
}

void D3D12Brixelizer::Cleanup( void )
{
    ModelInst = nullptr;

    // Free up resources in an orderly fashion
    Renderer::Shutdown();
}

void D3D12Brixelizer::LoadAssets(ComPtr<ID3D12GraphicsCommandList4>& CommandList)
{
    ComPtr<ID3D12Resource> VertexBufferUploadHeap;

    ufbx_load_opts opts = { 0 }; // Optional, pass NULL for defaults
    ufbx_error error; // Optional, pass NULL if you don't care about errors
    ufbx_scene* scene = ufbx_load_file("Assets/MiniatureNightCity.fbx", &opts, &error);
    if (!scene) {
        fprintf(stderr, "Failed to load: %s\n", error.description.data);
    }

    UINT num_vertices = (UINT)scene->meshes[0]->num_vertices;

    g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(num_vertices * sizeof(XMFLOAT3)),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&VertexBuffer));

    g_Device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT3)),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&VertexBufferUploadHeap));

    XMFLOAT3* vertices = new XMFLOAT3[num_vertices];

    D3D12_SUBRESOURCE_DATA vertexData = {};
    for (size_t i = 0; i < num_vertices; i++)
    {
        vertices[i].x = ufbx_get_vertex_vec3(&scene->meshes[0]->vertex_position, i).x;
        vertices[i].y = ufbx_get_vertex_vec3(&scene->meshes[0]->vertex_position, i).y;
        vertices[i].x = ufbx_get_vertex_vec3(&scene->meshes[0]->vertex_position, i).z;
    }

    vertexData.pData = vertices;
    vertexData.RowPitch = sizeof(XMFLOAT3);
    vertexData.SlicePitch = vertexData.RowPitch;

    UpdateSubresources<1>(CommandList.Get(), VertexBuffer.Get(), VertexBufferUploadHeap.Get(), 0, 0, 1, &vertexData);
    CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(VertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

    // Initialize the vertex buffer view.
    VertexBufferView.BufferLocation = VertexBuffer->GetGPUVirtualAddress();
    VertexBufferView.StrideInBytes = sizeof(XMFLOAT3);
    VertexBufferView.SizeInBytes = num_vertices * sizeof(XMFLOAT3);
}

void D3D12Brixelizer::Update( float deltaT )
{
    ScopedTimer _prof(L"Update State");

    // Update something
    CameraController->Update(deltaT);

    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Update");

    ModelInst.Update(gfxContext, deltaT);

    gfxContext.Finish();


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


    // Some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.  Everything
    // is static in our scene, so we generate velocity from camera motion and the depth buffer.  A velocity buffer
    // is necessary for all temporal effects (and motion blur).
    MotionBlur::GenerateCameraVelocityBuffer(gfxContext, Camera, true);

    TemporalEffects::ResolveImage(gfxContext);

    // Until I work out how to couple these two, it's "either-or".
    if (DepthOfField::Enable)
        DepthOfField::Render(gfxContext, Camera.GetNearClip(), Camera.GetFarClip());
    else
        MotionBlur::RenderObjectBlur(gfxContext, g_VelocityBuffer);





    gfxContext.Finish();



    /*
    ID3D12GraphicsCommandList* CommandList = gfxContext.GetCommandList();
    // Rendering something
    PIXBeginEvent(CommandList, 0, L"Draw cities");
            CommandList->SetPipelineState(pPso1);

            pCommandList->SetGraphicsRootDescriptorTable(2, cbvSrvHandle);
            cbvSrvHandle.Offset(cbvSrvDescriptorSize);

            pCommandList->DrawIndexedInstanced(numIndices, 1, 0, 0, 0);
    PIXEndEvent(CommandList);
    */







}
