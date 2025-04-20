#if !defined(HOST_DEVICE_COMMON)
#define HOST_DEVICE_COMMON


#ifdef __cplusplus
	#include "glm/glm.hpp"

	using mat4 = glm::mat4;
	using vec4 = glm::vec4;
	using vec3 = glm::vec3;
	using vec2 = glm::vec2;
#else
	#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
	#extension GL_EXT_shader_explicit_arithmetic_types_int32 : require
#endif

const uint32_t MAX_POINT_LIGHTS_PER_FRAME = 3;

struct WindowExtent
{
	uint32_t width;
	uint32_t height;
};

struct CameraDataGPU
{
	mat4 view;
	mat4 invView;
	mat4 proj;
	mat4 viewproj;
	mat4 invProj;
	mat4 invViewProj;
	mat4 prevViewProj;
};

struct DirectionalLightGPU
{
	vec4 direction;
	vec4 color; // a for intensity
};

struct PointLightGPU
{
	vec4 position;
	vec4 color; // a for intensity
};

struct LightingData
{
	vec4 ambientLight;

	DirectionalLightGPU dirLight;
	PointLightGPU pointLights[MAX_POINT_LIGHTS_PER_FRAME];
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
