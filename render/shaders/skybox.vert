#version 460

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;
layout (location = 4) in vec3 vTangent;

struct CameraData
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
};

layout (set = 1, binding = 0) uniform CameraBuffer
{
	CameraData camData;
} camSceneData;

layout (location = 0) out vec3 outUVW;

layout (push_constant) uniform constants
{
	mat4 renderMatrix;
} PushConstants;

void main()
{
	// normal transform, no non-uniform scaling

	vec4 vPositionWorld = PushConstants.renderMatrix * vec4(vPosition, 1.0);
	gl_Position = camSceneData.camData.viewproj * vPositionWorld;
	
	outUVW = vPosition;
}