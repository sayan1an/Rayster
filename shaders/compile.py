import subprocess

glslangValidator = "C:/VulkanSDK/1.1.106.0/Bin32/glslangValidator.exe"

compileList =[]
compileList.append(("./GBuffer/gBuf.vert", "./GBuffer/gBufVert.spv"))
compileList.append(("./GBuffer/gBuf.frag", "./GBuffer/gBufFrag.spv"))
compileList.append(("./GBuffer/gShow.vert", "./GBuffer/gShowVert.spv"))
compileList.append(("./GBuffer/gShow.frag", "./GBuffer/gShowFrag.spv"))

compileList.append(("./GBufferShow/gShow.vert", "./GBufferShow/gShowVert.spv"))
compileList.append(("./GBufferShow/gShow.frag", "./GBufferShow/gShowFrag.spv"))

compileList.append(("./ImGui/ui.vert", "./ImGui/uiVert.spv"))
compileList.append(("./ImGui/ui.frag", "./ImGui/uiFrag.spv"))

compileList.append(("./GraphicsComputeGraphicsApp/edgeDetect.comp", "./GraphicsComputeGraphicsApp/edgeDetectComp.spv"))
compileList.append(("./GraphicsComputeGraphicsApp/gShow.vert", "./GraphicsComputeGraphicsApp/gShowVert.spv"))
compileList.append(("./GraphicsComputeGraphicsApp/gShow.frag", "./GraphicsComputeGraphicsApp/gShowFrag.spv"))

compileList.append(("./RTXApp/01_raygen.rgen", "./RTXApp/01_raygen.spv"))
compileList.append(("./RTXApp/01_miss.rmiss", "./RTXApp/01_miss.spv"))
compileList.append(("./RTXApp/01_close.rchit", "./RTXApp/01_close.spv"))

try:
    for shader in compileList:
        output = subprocess.Popen([glslangValidator, "-V", shader[0], "-o", shader[1] ], stdout=subprocess.PIPE).communicate()[0]
        print(output.decode('utf-8'))
except FileNotFoundError:
    print("Path to glslangValidator is invalid!")