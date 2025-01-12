#ifndef HOST_DEVICE_COMMON
#define HOST_DEVICE_COMMON

#ifdef __cplusplus
	using mat4 = glm::mat4;
	using vec4 = glm::vec4;
	using vec3 = glm::vec3;
	using vec2 = glm::vec2;
#else
	#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
	#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#endif

// Temporarily hardcoded
const uint32_t NUM_LIGHTS = 3;

struct CameraData
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
	mat4 invProj;
	mat4 invViewProj;
	mat4 prevViewProj;
};

struct DirectionalLight
{
	vec4 direction; // w for sun power
	vec4 color;
};

struct PointLight
{
	vec4 position;
	vec4 color;
};

struct SceneData
{
	vec4 fogColor; // w for exponent
	vec4 fogDistances; // x -- min, y -- max
	vec4 ambientLight;

	DirectionalLight dirLight;
	PointLight pointLights[NUM_LIGHTS];
};

struct ObjectData
{
	mat4 model;
	int32_t matIndex;
	uint64_t vertexBufferAddress;
	uint64_t indexBufferAddress;
	vec3 emittance;
};

struct Vertex
{
	vec3 position;
	vec3 normal;
	vec3 color;
	vec2 uv;
	vec3 tangent;
};

struct RayPushConstants
{
	int32_t frame;

#ifdef __cplusplus
	int32_t USE_TEMPORAL_ACCUMULATION;
	int32_t USE_MOTION_VECTORS;
	int32_t USE_SHADER_EXECUTION_REORDERING;
#else
	bool USE_TEMPORAL_ACCUMULATION;
	bool USE_MOTION_VECTORS;
	bool USE_SHADER_EXECUTION_REORDERING;
#endif
	int32_t MAX_BOUNCES;

	int32_t padding[3];
};

#ifdef __cplusplus
enum class RTXSets
{
#else
const uint
#endif
	eDiffuseTex = 0,
	eMetallicTex = 1,
	eRoughnessTex = 2,
	eNormalMap = 3,
	eSkybox = 4,
	eObjectData = 5,
	ePerFrame = 6,
	eGeneralRTX = 7,
	eGlobal = 8
#ifdef __cplusplus
}
#endif
;

#endif // #ifndef HOST_DEVICE_COMMON
