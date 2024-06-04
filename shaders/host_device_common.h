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

struct SceneData
{
	vec4 fogColor; // w for exponent
	vec4 fogDistances; // x -- min, y -- max
	vec4 ambientLight;
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

struct ObjectData
{
	mat4 model;
	int matIndex;
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
	int frame;

	bool USE_TEMPORAL_ACCUMULATION;
	bool USE_MOTION_VECTORS;
	bool USE_SHADER_EXECUTION_REORDERING;
	int MAX_BOUNCES;
	uint NRC_TILE_WIDTH;
	uint trainingPathIndex;

	uint NRC_MODE;
};

const uint MAX_BOUNCES_LIMIT = 8;

struct ClassicTrainingData
{
	vec3 position;
	vec2 direction;
	vec2 normal;
	float roughness;
	vec3 diffuseAlbedo;
	vec3 specularReflectance;
};

struct DiffuseTrainingData
{
	vec3 position;
	vec2 normal;
	vec3 diffuseAlbedo;
	bool wasVisible;
	vec3 prevRadiance;
};

struct SpecularTrainingData
{
	vec3 position;
	vec2 direction;
	vec2 normal;
	float roughness;
	vec3 specularReflectance;
};


const uint // enum RTXSets
	eDiffuseTex = 0,
	eMetallicTex = 1,
	eRoughnessTex = 2,
	eNormalMap = 3,
	eSkybox = 4,
	eObjectData = 5,
	ePerFrame = 6,
	eGeneralRTX = 7,
	eGlobal = 8;

const uint // enum NeuralRadianceCacheMode
	eNone = 0,
	eClassic = 1,
	eDedicatedTemporalAdaptation = 2;

const uint // enum CacheType
	eCacheClassic = 0,
	eDiffuse = 1,
	eSpecular = 2;