#version 460

#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "host_device_common.h"

layout (location = 0) in vec2 inTexCoords;

layout (location = 0) out vec2 motionVector;

layout (set = 0, binding = 0) uniform CameraBuffer
{
	CameraData camData;
} camSceneData;

void main()
{
	vec4 positionNDC = vec4((inTexCoords) * 2.0 - 1.0, 1.0, 1.0);
	positionNDC.xy *= -1;

	vec4 positionWorld = camSceneData.camData.invViewProj * positionNDC;
	positionWorld /= positionWorld.w;
	
	vec4 prevPosNDC = camSceneData.camData.prevViewProj * vec4(positionWorld.xyz, 1.0);
	prevPosNDC /= prevPosNDC.w;

	vec2 prevPosScreen = (prevPosNDC.xy * vec2(-0.5, -0.5) + 0.5);
	motionVector = inTexCoords - prevPosScreen;
}