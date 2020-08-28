#pragma once

#include "GBuffer.hpp"
#include "GraphicsComputeGraphics.hpp"
#include "RtxComputeBase.hpp"
#include "RtxBasicApp.hpp"
#include "RtxGBuffer.hpp"
#include "RtxHardShadows.hpp"
#include "RtxHybridHardShadows.hpp"
#include "RtxHybridSoftShadows.hpp"
#include "RtxFiltering_0.hpp"
#include "RtxFiltering_1.hpp"
#include "RtxFiltering_2/RtxFiltering_2.hpp"

int main()
{	
	int select = 6;
	try {
		if (select == 0) {
			// Show Rasterization based GBuffer
			GBufferApplication app;
			app.run(1280, 720, false);
		}
		else if (select == 1) {
			// GBuffer Pass followed by compute shader pass
			// Also enables AA for GBuffer pass
			GraphicsComputeApplication app;
			app.run(1280, 720, true);
		}
		else if (select == 2) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			
			// It is same a GBuffer application but can be also used as a starting template for any compute/rtx application.
			RtxComputeBase app(instanceExtensions, deviceExtensions);
			app.run(1280, 720, false);
		}
		else if (select == 3) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			
			// Simplest raytracing application
			// Primary/Camera rays are ray-traced and not rasterized
			// Useful for measuring primary/camera ray performace
			RtxBasicApplication app(instanceExtensions, deviceExtensions);
			app.run(1280, 720, false);
		}
		else if (select == 4) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };

			// GBuffer application with primary rays traced using ray-tracing
			// Useful for comparing ray-traced GBuffer with rasterized GBuffer.
			RtxGBufferApplication app(instanceExtensions, deviceExtensions);
			app.run(1280, 720, false);
		}
		else if (select == 5) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			
			// Primary and shadow rays cast using ray-tracing with a point light source
			RtxHardShadowApplication app(instanceExtensions, deviceExtensions);
			app.run(1280, 720, false);
		}
		else if (select == 6) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			
			// GBuffer pass with rasterization and shadow ray cast with ray-tracing with a point light source.
			RtxHybridHardShadows app(instanceExtensions, deviceExtensions);
			app.run(1280, 720, false);
		}
		else if (select == 7) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			std::vector<const char*> deviceFeatures = { "shaderStorageImageExtendedFormats" };

			// GBuffer pass with rasterization and shadow ray cast with ray-tracing with a point light and area source.
			RtxHybridSoftShadows app(instanceExtensions, deviceExtensions, deviceFeatures);
			app.run(1280, 720, false);
		}
		else if (select == 8) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			std::vector<const char*> deviceFeatures = { "shaderStorageImageExtendedFormats" };
			// experimental technique, samples move across the world space directions in time
			RtxFiltering_0 app(instanceExtensions, deviceExtensions, deviceFeatures);
			app.run(1280, 720, false);
		}
		else if (select == 9) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			std::vector<const char*> deviceFeatures = { "shaderStorageImageExtendedFormats" };
			// experimental technique, samples move across the emitter space in time and implements pixel reprorojection in time
			RtxFiltering_1 app(instanceExtensions, deviceExtensions, deviceFeatures);
			app.run(1280, 720, false);
		}
		else if (select == 10) {
			std::vector<const char*> deviceExtensions = { VK_NV_RAY_TRACING_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME };
			std::vector<const char*> instanceExtensions = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
			std::vector<const char*> deviceFeatures = { "shaderStorageImageExtendedFormats" };
			// experimental technique, samples move across the emitter space in time and implements pixel reprorojection in time
			RtxFiltering_2 app(instanceExtensions, deviceExtensions, deviceFeatures);
			app.run(1280, 720, false);
		}
		
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		//return EXIT_FAILURE;
	}

	int i;
	std::cin >> i;
	return EXIT_SUCCESS;
}