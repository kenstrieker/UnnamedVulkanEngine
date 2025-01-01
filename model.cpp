#include "model.hpp"
#include "utils.hpp"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <cassert>
#include <unordered_map>

namespace std {
	template <>
	struct hash<engine::model::Vertex> {
		size_t operator()(engine::model::Vertex const& vertex) const {
			size_t seed = 0;
			engine::hashCombine(seed, vertex.position, vertex.color, vertex.normal, vertex.uv);
			return seed;
		}
	};
}

namespace engine {
	model::model(device& deviceInstance, const model::Builder& builderInstance) : deviceInstance{ deviceInstance } {
		createVertexBuffers(builderInstance.vertices);
		createIndexBuffer(builderInstance.indices);
	}

	model::~model() {
		vkDestroyBuffer(deviceInstance.getDevice(), vertexBuffer, nullptr);
		vkFreeMemory(deviceInstance.getDevice(), vertexBufferMemory, nullptr);

		if (hasIndexBuffer) {
			vkDestroyBuffer(deviceInstance.getDevice(), indexBuffer, nullptr);
			vkFreeMemory(deviceInstance.getDevice(), indexBufferMemory, nullptr);
		}
	}

	std::unique_ptr<model> model::createModelFromFile(device& deviceInstance, const std::string& filepath) {
		Builder builderInstance = {};
		builderInstance.loadModel(filepath);
		return std::make_unique<model>(deviceInstance, builderInstance);
	}

	void model::createVertexBuffers(const std::vector<Vertex>& vertices) {
		// check that we have at least one triangle (3 vertices)
		vertexCount = static_cast<uint32_t>(vertices.size());
		assert(vertexCount >= 3 && "Vertex count must be at least 3");

		// create a staging buffer
		VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		deviceInstance.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);
		
		// map the staging buffer memory
		void* data;
		vkMapMemory(deviceInstance.getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
		vkUnmapMemory(deviceInstance.getDevice(), stagingBufferMemory);

		// create a vertex buffer
		deviceInstance.createBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);
		deviceInstance.copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

		vkDestroyBuffer(deviceInstance.getDevice(), stagingBuffer, nullptr);
		vkFreeMemory(deviceInstance.getDevice(), stagingBufferMemory, nullptr);
	}

	void model::createIndexBuffer(const std::vector<uint32_t>& indices) {
		// check that we are using an index buffer for rendering
		indexCount = static_cast<uint32_t>(indices.size());
		hasIndexBuffer = indexCount > 0;
		if (!hasIndexBuffer) return;

		// create a staging buffer
		VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		deviceInstance.createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

		// map the staging buffer memory
		void* data;
		vkMapMemory(deviceInstance.getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
		memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
		vkUnmapMemory(deviceInstance.getDevice(), stagingBufferMemory);

		// create a vertex buffer
		deviceInstance.createBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);
		deviceInstance.copyBuffer(stagingBuffer, indexBuffer, bufferSize);

		vkDestroyBuffer(deviceInstance.getDevice(), stagingBuffer, nullptr);
		vkFreeMemory(deviceInstance.getDevice(), stagingBufferMemory, nullptr);
	}

	void model::bind(VkCommandBuffer commandBuffer) {
		VkBuffer buffers[] = { vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

		if (hasIndexBuffer) {
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}
	}

	void model::draw(VkCommandBuffer commandBuffer) {
		if (hasIndexBuffer) {
			vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
		}
		else {
			vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
		}
	}

	std::vector<VkVertexInputBindingDescription> model::Vertex::getBindingDescriptions() {
		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
		bindingDescriptions[0].binding = 0;
		bindingDescriptions[0].stride = sizeof(Vertex);
		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return bindingDescriptions;
	}

	std::vector<VkVertexInputAttributeDescription> model::Vertex::getAttributeDescriptions() {
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions(2);
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, position);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		return attributeDescriptions;
	}

	void model::Builder::loadModel(const std::string& filepath) {
		tinyobj::attrib_t attrib;
		std::vector<tinyobj::shape_t> shapes;
		std::vector<tinyobj::material_t> materials;
		std::string warn, err;

		if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filepath.c_str())) {
			throw std::runtime_error(warn + err);
		}

		// start from a fresh builder state
		vertices.clear();
		indices.clear();

		std::unordered_map<Vertex, uint32_t> uniqueVertices = {};

		for (const auto& shape : shapes) {
			for (const auto& index : shape.mesh.indices) {
				Vertex vertexInstance = {};

				if (index.vertex_index >= 0) {
					vertexInstance.position = { 
						attrib.vertices[3 * index.vertex_index + 0],
						attrib.vertices[3 * index.vertex_index + 1],
						attrib.vertices[3 * index.vertex_index + 2],
					};

					auto colorIndex = 3 * index.vertex_index + 2;
					if (colorIndex < attrib.colors.size()) {
						vertexInstance.color = {
							attrib.colors[colorIndex - 2],
							attrib.colors[colorIndex - 1],
							attrib.colors[colorIndex - 0],
						};
					}
					else {
						vertexInstance.color = { 1.f, 1.f, 1.f };
					}
				}

				if (index.normal_index >= 0) {
					vertexInstance.normal = {
						attrib.vertices[3 * index.normal_index + 0],
						attrib.vertices[3 * index.normal_index + 1],
						attrib.vertices[3 * index.normal_index + 2],
					};
				}

				if (index.texcoord_index >= 0) {
					vertexInstance.uv = {
						attrib.texcoords[2 * index.texcoord_index + 0],
						attrib.texcoords[2 * index.texcoord_index + 1],
					};
				}

				if (uniqueVertices.count(vertexInstance) == 0) {
					uniqueVertices[vertexInstance] = static_cast<uint32_t>(vertices.size());
					vertices.push_back(vertexInstance);
				}
				indices.push_back(uniqueVertices[vertexInstance]);
			}
		}
	}
}