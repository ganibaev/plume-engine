#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;
layout (location = 4) in uint vMatID;
layout (location = 5) in vec3 vTangent;

struct CameraData
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
};

layout (set = 0, binding = 0) uniform CameraBuffer
{
	CameraData camData;
} camSceneData;

struct ObjectData
{
	mat4 model;
};

// object matrices 
layout (std140, set = 1, binding = 0) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;

layout (location = 0) out vec3 outUVW;

void main()
{
	mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;

	// normal transform, no non-uniform scaling

	vec4 vPositionWorld = modelMatrix * vec4(vPosition, 1.0);
	gl_Position = camSceneData.camData.viewproj * vPositionWorld;
	
	outUVW = vPosition;
}