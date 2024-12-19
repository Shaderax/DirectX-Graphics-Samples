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

float FindClosestTriangle(uint3 VoxelPos, uint GI)
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

RWTexture2D<float> depthTex : register(u0);

#define _RootSig \
    "RootFlags(0), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0, numDescriptors = 1))," \
    "DescriptorTable(UAV(u0, numDescriptors = 1))"

[RootSignature(_RootSig)]
[numthreads(32, 32, 1)]
void main(
    uint2 Gid : SV_GroupID,
    uint2 GTid : SV_GroupThreadID,
    uint GI : SV_GroupIndex,
    uint3 DTid : SV_DispatchThreadID)
{
    uint3 VoxelPos = uint3(GTid.x, Gid);
    uint X = Gid.x * 32 + GTid.x;
    uint Y = Gid.y * 32 + GTid.y;
    uint2 TexPos2D = uint2(X, Y);

    // Find closest Triangle
    depthTex[TexPos2D] = GTid.y; // FindClosestTriangle(VoxelPos, GI);
}
