import subprocess

glslangValidator = "C:/VulkanSDK/1.1.106.0/Bin32/glslangValidator.exe"

compileList =[]
compileList.append(("./GBuffer/gBuf.vert", "./GBuffer/gBufVert.spv"))
compileList.append(("./GBuffer/gBuf.frag", "./GBuffer/gBufFrag.spv"))
compileList.append(("./GBuffer/gShow.vert", "./GBuffer/gShowVert.spv"))
compileList.append(("./GBuffer/gShow.frag", "./GBuffer/gShowFrag.spv"))

compileList.append(("./ImGui/ui.vert", "./ImGui/uiVert.spv"))
compileList.append(("./ImGui/ui.frag", "./ImGui/uiFrag.spv"))

compileList.append(("./GraphicsComputeGraphicsApp/edgeDetect.comp", "./GraphicsComputeGraphicsApp/edgeDetectComp.spv"))
compileList.append(("./GraphicsComputeGraphicsApp/gShow.vert", "./GraphicsComputeGraphicsApp/gShowVert.spv"))
compileList.append(("./GraphicsComputeGraphicsApp/gShow.frag", "./GraphicsComputeGraphicsApp/gShowFrag.spv"))

compileList.append(("./RtxBasicApp/01_raygen.rgen", "./RtxBasicApp/01_raygen.spv"))
compileList.append(("./RtxBasicApp/01_miss.rmiss", "./RtxBasicApp/01_miss.spv"))
compileList.append(("./RtxBasicApp/01_close.rchit", "./RtxBasicApp/01_close.spv"))
compileList.append(("./RtxBasicApp/gShow.vert", "./RtxBasicApp/gShowVert.spv"))
compileList.append(("./RtxBasicApp/gShow.frag", "./RtxBasicApp/gShowFrag.spv"))

compileList.append(("./RtxGBuffer/01_raygen.rgen", "./RtxGBuffer/01_raygen.spv"))
compileList.append(("./RtxGBuffer/01_miss.rmiss", "./RtxGBuffer/01_miss.spv"))
compileList.append(("./RtxGBuffer/01_close.rchit", "./RtxGBuffer/01_close.spv"))
compileList.append(("./RtxGBuffer/gShow.vert", "./RtxGBuffer/gShowVert.spv"))
compileList.append(("./RtxGBuffer/gShow.frag", "./RtxGBuffer/gShowFrag.spv"))

compileList.append(("./RtxHardShadows/01_raygen.rgen", "./RtxHardShadows/01_raygen.spv"))
compileList.append(("./RtxHardShadows/01_miss.rmiss", "./RtxHardShadows/01_miss.spv"))
compileList.append(("./RtxHardShadows/02_miss.rmiss", "./RtxHardShadows/02_miss.spv"))
compileList.append(("./RtxHardShadows/01_close.rchit", "./RtxHardShadows/01_close.spv"))
#compileList.append(("./RtxHardShadows/02_close.rchit", "./RtxHardShadows/02_close.spv"))
compileList.append(("./RtxHardShadows/gShow.vert", "./RtxHardShadows/gShowVert.spv"))
compileList.append(("./RtxHardShadows/gShow.frag", "./RtxHardShadows/gShowFrag.spv"))

compileList.append(("./RtxComputeBase/gShow.vert", "./RtxComputeBase/gShowVert.spv"))
compileList.append(("./RtxComputeBase/gShow.frag", "./RtxComputeBase/gShowFrag.spv"))

compileList.append(("./RtxHybridHardShadows/gShow.vert", "./RtxHybridHardShadows/gShowVert.spv"))
compileList.append(("./RtxHybridHardShadows/gShow.frag", "./RtxHybridHardShadows/gShowFrag.spv"))
compileList.append(("./RtxHybridHardShadows/01_raygen.rgen", "./RtxHybridHardShadows/01_raygen.spv"))
compileList.append(("./RtxHybridHardShadows/01_miss.rmiss", "./RtxHybridHardShadows/01_miss.spv"))

compileList.append(("./RtxHybridSoftShadows/gShow.vert", "./RtxHybridSoftShadows/gShowVert.spv"))
compileList.append(("./RtxHybridSoftShadows/gShow.frag", "./RtxHybridSoftShadows/gShowFrag.spv"))
compileList.append(("./RtxHybridSoftShadows/01_raygen.rgen", "./RtxHybridSoftShadows/01_raygen.spv"))
compileList.append(("./RtxHybridSoftShadows/01_miss.rmiss", "./RtxHybridSoftShadows/01_miss.spv"))
compileList.append(("./RtxHybridSoftShadows/01_close.rchit", "./RtxHybridSoftShadows/01_close.spv"))

try:
    for shader in compileList:
        output = subprocess.Popen([glslangValidator, "-V", shader[0], "-o", shader[1] ], stdout=subprocess.PIPE).communicate()[0]
        print(output.decode('utf-8'))
except FileNotFoundError:
    print("Path to glslangValidator is invalid!")