//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):	Alex Nankervis
//

#define FLT_MIN         1.175494351e-38F        // min positive value
#define FLT_MAX         3.402823466e+38F        // max value
#define PI				3.1415926535f
#define TWOPI			6.283185307f

struct Vertex
{
    float3 Position;
    float3 Normal;
};

float dot2(in float3 v)
{
    return dot(v, v);
}

float udTriangle(float3 p, float3 a, float3 b, float3 c)
{
    float3 ba = b - a;
    float3 pa = p - a;
    float3 cb = c - b;
    float3 pb = p - b;
    float3 ac = a - c;
    float3 pc = p - c;
    float3 nor = cross(ba, ac);

    return sqrt(
    (sign(dot(cross(ba, nor), pa)) +
     sign(dot(cross(cb, nor), pb)) +
     sign(dot(cross(ac, nor), pc)) < 2.0)
     ?
     min(min(
     dot2(ba * clamp(dot(ba, pa) / dot2(ba), 0.0, 1.0) - pa),
     dot2(cb * clamp(dot(cb, pb) / dot2(cb), 0.0, 1.0) - pb)),
     dot2(ac * clamp(dot(ac, pc) / dot2(ac), 0.0, 1.0) - pc))
     :
     dot(nor, pa) * dot(nor, pa) / dot2(nor));
}

cbuffer CSConstants : register(b0)
{
    float3 Center;
    uint NumTriangles;
};

StructuredBuffer<Vertex> TrianglesBuffer : register(t0);

float FindClosestTriangle(uint3 VoxelPos)
{
    uint ClosestTriangleID = 0;
    float ClosestDistance = FLT_MAX;

    uint i = 0;
    for (; i < NumTriangles ; i++)
    {
        Vertex a = TrianglesBuffer[i * 3 + 0];
        Vertex b = TrianglesBuffer[i * 3 + 1];
        Vertex c = TrianglesBuffer[i * 3 + 2];

        float Distance = udTriangle(float3(VoxelPos.x, VoxelPos.y, VoxelPos.z), a.Position, b.Position, c.Position);

        if (Distance < ClosestDistance)
        {
            ClosestDistance = Distance;
            ClosestTriangleID = i;
        }
    }

    return ClosestDistance;
}

uint2 Get2dPos(uint id)
{
    uint X = id % 512;
    
    uint tX = id / 512;
    uint Y = tX;// % 512;
    
    return uint2(X, Y);
}

uint3 Get3dPos(uint id)
{
    uint X = id % 64;
    
    uint tX = id / 64;
    uint Y = tX % 64;

    uint Z = tX / 4096;

    return uint3(X, Y, Z);
}

RWTexture2D<float> depthTex : register(u0);

#define _RootSig \
    "RootFlags(0), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 1))," \
    "DescriptorTable(UAV(u0, numDescriptors = 1))"

//[numthreads(32, 32, 1)]
[RootSignature(_RootSig)]
[numthreads(256, 1, 1)]
void main(
    uint2 Gid : SV_GroupID, // id du thread groupe
    uint2 GTid : SV_GroupThreadID, // id du thread dans le thread group
    uint GI : SV_GroupIndex,
    uint3 DTid : SV_DispatchThreadID)
{
    uint3 VoxelPos = Get3dPos(DTid.x);
    uint2 TexPos2D = Get2dPos(DTid.x);

//    uint X = Gid.x * 32 + GTid.x;
//    uint Y = Gid.y * 32 + GTid.y;
//    uint2 TexPos2D = uint2(X, Y);

    // Find closest Triangle
    depthTex[TexPos2D] = FindClosestTriangle(VoxelPos);
}
