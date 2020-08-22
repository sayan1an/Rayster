git clone --recurse-submodules https://github.com/sayan1an/Rayster

Visual Studio->Properties->general->c++ language standart c++17

Include folders

..Rayster\external\glfw-3.2.1.bin.WIN64\include
..Rayster\external\imgui
..Rayster\external\implot
..Rayster\external\tinyobjloader
..Rayster\external\stb
..Rayster\external\glm
..Rayster\external\spline\src
..Rayster\external\cereal\include
C:\VulkanSDK\1.1.106.0\Include
..Rayster\external\VulkanMemoryAllocator\src
Optional:
    D:\vcpkg\installed\x86-windows\include // this is for assimp binaries

Lib folders

..Rayster\external\glfw-3.2.1.bin.WIN64\lib-vc2015
C:\VulkanSDK\1.1.106.0\Lib
Optional:
    D:\vcpkg\installed\x86-windows\lib // this is for assimp binaries

Lib files

glfw3.lib
vulkan-1.lib
Optional:
    assimp-vc142-mt.lib // this is for assimp binaries

Note for imgui and implot

Add all imgui/implot files in your project