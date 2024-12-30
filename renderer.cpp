#include "renderer.hpp"
#include <stdexcept>
#include <array>

namespace engine {
	renderer::renderer(window& windowInstance, device& deviceInstance) : windowInstance{ windowInstance }, deviceInstance{ deviceInstance } {
		recreateSwapchain();
		createCommandBuffers();
	}

	renderer::~renderer() {
		freeCommandBuffers();
	}

	void renderer::recreateSwapchain() {
		// get the current window size
		auto extent = windowInstance.getExtent();

		// update extent while size is valid, will pause and wait during minimization
		while (extent.width == 0 || extent.height == 0) {
			extent = windowInstance.getExtent();
			glfwWaitEvents();
		}

		// wait until the current swap chain is no longer being used before we create another
		vkDeviceWaitIdle(deviceInstance.getDevice());
		// swapchainInstance = nullptr; // temporary fix; two swap chains can't coexist on the same window, so destroy the old one first

		if (swapchainInstance == nullptr) {
			swapchainInstance = std::make_unique<swapchain>(deviceInstance, extent);
		}
		else {
			std::shared_ptr<swapchain> oldSwapchainInstance = std::move(swapchainInstance);
			swapchainInstance = std::make_unique<swapchain>(deviceInstance, extent, oldSwapchainInstance);

			if (!oldSwapchainInstance->compareSwapFormats(*swapchainInstance.get())) {
				throw std::runtime_error("swap chain image or depth format has changed!");
			}
		}
	}

	void renderer::createCommandBuffers() {
		// // resize the container holding the command buffers
		commandBuffers.resize(swapchain::MAX_FRAMES_IN_FLIGHT);

		// allocate the command buffers by specifying the command pool and number of buffers to allocate
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandPool = deviceInstance.getCommandPool();
		allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

		// create the command buffers
		if (vkAllocateCommandBuffers(deviceInstance.getDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate command buffers!");
		}
	}

	void renderer::freeCommandBuffers() {
		vkFreeCommandBuffers(deviceInstance.getDevice(), deviceInstance.getCommandPool(), static_cast<float>(commandBuffers.size()), commandBuffers.data());
		commandBuffers.clear();
	}

	VkCommandBuffer renderer::beginFrame() {
		assert(!isFrameStarted && "Can't call beginFrame while already in progress");

		// acquire an image from the swap chain
		auto result = swapchainInstance->acquireNextImage(&currentImageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapchain();
			return nullptr; // the frame has not successfully started
		}

		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("failed to acquire swap chain image!");
		}

		isFrameStarted = true; // the frame has started

		// begin recording command buffers
		auto commandBuffer = getCurrentCommandBuffer();		
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
			throw std::runtime_error("failed to begin recording command buffer!");
		}

		return commandBuffer;
	}


	VkCommandBuffer renderer::endFrame() {
		assert(isFrameStarted && "Can't call endFrame while frame is not in progress");

		// record and  submit the command buffer
		auto commandBuffer = getCurrentCommandBuffer();
		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
		}
		auto result = swapchainInstance->submitCommandBuffers(&commandBuffer, &currentImageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || windowInstance.wasWindowResized()) {
			windowInstance.resetWindowResizedFlag();
			recreateSwapchain();
		}
		else if (result != VK_SUCCESS) {
			throw std::runtime_error("failed to present swap chain image!");
		}

		isFrameStarted = false;
		currentFrameIndex = (currentFrameIndex + 1) % swapchain::MAX_FRAMES_IN_FLIGHT;
	}

	void renderer::beginSwapchainRenderPass(VkCommandBuffer commandBuffer) {
		assert(isFrameStarted && "Can't call beginSwapchainRenderPass if frame is not in progress");
		assert(commandBuffer == getCurrentCommandBuffer() && "Can't begin render pass on command buffer from a different frame");

		// start defining a render pass, creating a framebuffer for each swap chain image where it is specified as a color attachment
		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = swapchainInstance->getRenderPass();
		renderPassInfo.framebuffer = swapchainInstance->getFrameBuffer(currentImageIndex);

		// define the size of the render area
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapchainInstance->getSwapchainExtent();

		// define clear values to use for the color attachment
		std::array<VkClearValue, 2> clearValues = {};
		clearValues[0].color = { 0.01f, 0.1f, 0.1f, 1.0f };
		clearValues[1].depthStencil = { 1.0f, 0 };
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		// record to our command buffer to begin the render pass
		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// configure the dynamic viewport and scissor
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(swapchainInstance->getSwapchainExtent().width);
		viewport.height = static_cast<float>(swapchainInstance->getSwapchainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{ {0, 0}, swapchainInstance->getSwapchainExtent() };
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}

	void renderer::endSwapchainRenderPass(VkCommandBuffer commandBuffer) {
		assert(isFrameStarted && "Can't call endSwapchainRenderPass if frame is not in progress");
		assert(commandBuffer == getCurrentCommandBuffer() && "Can't end render pass on command buffer from a different frame");

		vkCmdEndRenderPass(commandBuffer);
	}
}

