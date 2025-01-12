#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_scalar_block_layout : enable

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;
layout (location = 4) in vec3 vTangent;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 texCoord;
layout (location = 2) flat out uint matID;
layout (location = 3) out vec3 fragPosWorld;
layout (location = 4) out vec3 fragNormalWorld;
layout (location = 5) out vec3 fragTangent;

struct CameraData
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
};

layout (set = 5, binding = 0) uniform CameraBuffer
{
	CameraData camData;
} camSceneData;

struct ObjectData
{
	mat4 model;
	int matIndex;
	uint64_t indexBufferAddress;
	uint64_t vertexBufferAddress;
	vec3 emittance;
};

// object data 
layout (set = 4, binding = 0, scalar) readonly buffer ObjectBuffer
{
	ObjectData objects[];
} objectBuffer;

void main()
{
	mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;

	// normal transform, no non-uniform scaling
	fragNormalWorld = normalize(modelMatrix * vec4(vNormal, 0.0)).xyz;

	vec4 vPositionWorld = modelMatrix * vec4(vPosition, 1.0);
	fragPosWorld = vPositionWorld.xyz;

	gl_Position = camSceneData.camData.viewproj * vPositionWorld;

	outColor = vColor;
	texCoord = vTexCoord;
	matID = objectBuffer.objects[gl_BaseInstance].matIndex;
	fragTangent = vTangent;
}