#include "render_types.h"
#include <vector>

namespace Render
{

// DON'T FORGET:
// When registering a new descriptor set, add a new RegisteredDescriptorSet to enum class
// and to DescriptorSetFlagBits.
// 
// Descriptor set bind order in the shaders must be the same as in the RegisteredDescriptorSet
// and DescriptorSetFlagBits enums

enum DescriptorSetFlagBits
{
	eDiffuseTextures = 1 << 0,
	eMetallicTextures = 1 << 1,
	eRoughnessTextures = 1 << 2,
	eNormalMapTextures = 1 << 3,
	eSkyboxTextures = 1 << 4,
	eObjects = 1 << 5,
	eGBuffer = 1 << 6,
	ePostprocess = 1 << 7,
	eRTXPerFrame = 1 << 8,
	eRTXGeneral = 1 << 9,
	eGlobal = 1 << 10,
	eTLAS = 1 << 11
};

typedef uint32_t DescriptorSetFlags;


enum class RegisteredDescriptorSet
{
	eDiffuseTextures,
	eMetallicTextures,
	eRoughnessTextures,
	eNormalMapTextures,
	eSkyboxTextures,
	eObjects,
	eGBuffer,
	ePostprocess,
	eRTXPerFrame,
	eRTXGeneral,
	eGlobal,
	eTLAS,

	eMaxValue
};


class DescriptorManager
{
public:
	struct BufferInfo
	{
		vk::Buffer buffer;
		vk::DeviceSize offset;
		vk::DeviceSize range;
		vk::DescriptorType bufferType;
	};

	struct ImageInfo
	{
		vk::ImageView imageView;
		vk::ImageLayout layout;
		vk::Sampler sampler;
		vk::DescriptorType imageType;
	};


	struct DescriptorSetInfo
	{
		std::array<vk::DescriptorSet, FRAME_OVERLAP> sets;
		vk::DescriptorSetLayout setLayout;

		std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;

		std::vector<vk::DescriptorSet*> writeToSetsAt;
		std::vector<vk::WriteDescriptorSet> writes;
		std::vector<vk::WriteDescriptorSetAccelerationStructureKHR> accelWrites;
		std::vector<vk::AccelerationStructureKHR> accels;

		std::vector<vk::DescriptorImageInfo> imageInfos;
		std::vector<vk::DescriptorBufferInfo> bufferInfos;

		bool isPerFrame = false;
		bool hasBindless = false;
	};

	void init(vk::Device* pDevice, DeletionQueue* pDeletionQueue);

	void allocate_sets();
	void update_sets();

	void register_buffer(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
		const std::vector<BufferInfo>& bufferInfos, uint32_t binding, uint32_t numDescs = 1, bool isPerFrame = false);

	void register_image(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
		const std::vector<ImageInfo>& imageInfos, uint32_t binding, uint32_t numDescs = 1, bool isBindless = false,
		bool isPerFrame = false);

	void register_accel_structure(RegisteredDescriptorSet descriptorSetType, vk::ShaderStageFlags shaderStages,
		vk::AccelerationStructureKHR accelStructure, uint32_t binding, bool isPerFrame = false);

	std::vector<vk::DescriptorSetLayout> get_layouts(DescriptorSetFlags usedDscMask);

	std::vector<vk::DescriptorSet> get_descriptor_sets(DescriptorSetFlags usedDscMask, uint8_t perFrameId);

	static constexpr uint32_t NUM_DESCRIPTOR_SETS = static_cast<int>(RegisteredDescriptorSet::eMaxValue);
private:
	std::vector<DescriptorSetInfo> _setQueue;

	vk::Device* _pDevice;
	DeletionQueue* _pDeletionQueue;
	vk::DescriptorPool _pool;
};

} // namespace Render