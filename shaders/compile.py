import subprocess, os, sys

glslangValidator = "C:/VulkanSDK/1.2.148.1/Bin32/glslangValidator.exe"

# modifying the following files will cause a full compilation
forceFullCompilationList = []
forceFullCompilationList.append(("./commonMath.h", "null"))
forceFullCompilationList.append(("./hostDeviceShared.h", "null"))
forceFullCompilationList.append(("./Filters/filterParams.h", "null"))
forceFullCompilationList.append(("./RtxFiltering_2/hostDeviceShared.h", "null"))
forceFullCompilationList.append(("./RtxFiltering_3/hostDeviceShared.h", "null"))

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
compileList.append(("./RtxHybridSoftShadows/02_miss.rmiss", "./RtxHybridSoftShadows/02_miss.spv"))
compileList.append(("./RtxHybridSoftShadows/02_close.rchit", "./RtxHybridSoftShadows/02_close.spv"))

compileList.append(("./RtxFiltering_0/gShow.vert", "./RtxFiltering_0/gShowVert.spv"))
compileList.append(("./RtxFiltering_0/gShow.frag", "./RtxFiltering_0/gShowFrag.spv"))
compileList.append(("./RtxFiltering_0/biased/01_raygen.rgen", "./RtxFiltering_0/biased/01_raygen.spv"))
compileList.append(("./RtxFiltering_0/biased/01_miss.rmiss", "./RtxFiltering_0/biased/01_miss.spv"))
compileList.append(("./RtxFiltering_0/biased/01_close.rchit", "./RtxFiltering_0/biased/01_close.spv"))

compileList.append(("./RtxFiltering_1/gShow.vert", "./RtxFiltering_1/gShowVert.spv"))
compileList.append(("./RtxFiltering_1/gShow.frag", "./RtxFiltering_1/gShowFrag.spv"))
compileList.append(("./RtxFiltering_1/01_raygen.rgen", "./RtxFiltering_1/01_raygen.spv"))
compileList.append(("./RtxFiltering_1/01_miss.rmiss", "./RtxFiltering_1/01_miss.spv"))
compileList.append(("./RtxFiltering_1/02_miss.rmiss", "./RtxFiltering_1/02_miss.spv"))
compileList.append(("./RtxFiltering_1/02_close.rchit", "./RtxFiltering_1/02_close.spv"))

compileList.append(("./RtxFiltering_2/gBuf.frag", "./RtxFiltering_2/gBufFrag.spv"))
compileList.append(("./RtxFiltering_2/gShow.vert", "./RtxFiltering_2/gShowVert.spv"))
compileList.append(("./RtxFiltering_2/gShow.frag", "./RtxFiltering_2/gShowFrag.spv"))
compileList.append(("./RtxFiltering_2/raygen.rgen", "./RtxFiltering_2/raygen.spv"))
compileList.append(("./RtxFiltering_2/0_miss.rmiss", "./RtxFiltering_2/0_miss.spv"))
compileList.append(("./RtxFiltering_2/0_close.rchit", "./RtxFiltering_2/0_close.spv"))
compileList.append(("./RtxFiltering_2/1_close.rchit", "./RtxFiltering_2/1_close.spv"))
compileList.append(("./RtxFiltering_2/subSample.comp", "./RtxFiltering_2/subSample.spv"))
compileList.append(("./RtxFiltering_2/stencilPass.comp", "./RtxFiltering_2/stencilPass.spv"))
compileList.append(("./RtxFiltering_2/stencilCompositionPass.comp", "./RtxFiltering_2/stencilCompositionPass.spv"))
compileList.append(("./RtxFiltering_2/rtxCompositionPass.comp", "./RtxFiltering_2/rtxCompositionPass.spv"))
compileList.append(("./RtxFiltering_2/temporalFilter.comp", "./RtxFiltering_2/temporalFilter.spv"))
compileList.append(("./RtxFiltering_2/mcNoVis.comp", "./RtxFiltering_2/mcNoVis.spv"))

compileList.append(("./RtxFiltering_3/gBuf.frag", "./RtxFiltering_3/gBufFrag.spv"))
compileList.append(("./RtxFiltering_3/gShow.vert", "./RtxFiltering_3/gShowVert.spv"))
compileList.append(("./RtxFiltering_3/gShow.frag", "./RtxFiltering_3/gShowFrag.spv"))
compileList.append(("./RtxFiltering_3/raygen.rgen", "./RtxFiltering_3/raygen.spv"))
compileList.append(("./RtxFiltering_3/0_miss.rmiss", "./RtxFiltering_3/0_miss.spv"))
compileList.append(("./RtxFiltering_3/0_close.rchit", "./RtxFiltering_3/0_close.spv"))
compileList.append(("./RtxFiltering_3/1_close.rchit", "./RtxFiltering_3/1_close.spv"))
compileList.append(("./RtxFiltering_3/subSample.comp", "./RtxFiltering_3/subSample.spv"))
compileList.append(("./RtxFiltering_3/stencilPass.comp", "./RtxFiltering_3/stencilPass.spv"))
compileList.append(("./RtxFiltering_3/stencilCompositionPass.comp", "./RtxFiltering_3/stencilCompositionPass.spv"))
compileList.append(("./RtxFiltering_3/rtxCompositionPass.comp", "./RtxFiltering_3/rtxCompositionPass.spv"))
compileList.append(("./RtxFiltering_3/temporalFilter.comp", "./RtxFiltering_3/temporalFilter.spv"))
compileList.append(("./RtxFiltering_3/mcNoVis.comp", "./RtxFiltering_3/mcNoVis.spv"))
compileList.append(("./RtxFiltering_3/computeBlendeWeight.comp", "./RtxFiltering_3/computeBlendeWeight.spv"))

compileList.append(("./Filters/crossBilateralFilter.comp", "./Filters/crossBilateralFilter.spv"))
compileList.append(("./Filters/temporalFilter.comp", "./Filters/temporalFilter.spv"))
compileList.append(("./Filters/temporalWindowFilter.comp", "./Filters/temporalWindowFilter.spv"))
compileList.append(("./Filters/dummyFilter.comp", "./Filters/dummyFilter.spv"))
compileList.append(("./Filters/temporalFrequencyFilter.comp", "./Filters/temporalFrequencyFilter.spv"))

compileAll = False
if len(sys.argv) > 1:
    compileAll = True

def forceCompileAll():
    try:
        f = open("lastModifiedForced.temp")
    except IOError:
        return True
    
    for item in forceFullCompilationList:
        fName = f.readline()[:-1]
        if fName != item[0]:
            f.close()
            return True
        
        time = int(f.readline())
        if time < int(os.path.getmtime(item[0])):
            return True

    f.close()
    return False

def setLastModified(fName, list):
    f = open(fName,"w")

    for item in list:
        time = int(os.path.getmtime(item[0]))
        f.writelines([item[0] + "\n", str(time) + "\n"])
        
    f.flush()
    f.close()

def getLastModified():
    try:
        f = open("lastModified.temp")
    except IOError:
        return [0] * len(compileList)
    
    lastModified = []
    for shader in compileList:
        fName = f.readline()[:-1]
        if fName != shader[0]:
            f.close()
            return [0] * len(compileList)
        
        time = f.readline()
        if not os.path.exists(shader[1]):
            time = 0
        lastModified.append(int(time))

    f.close()
    return lastModified

lastModifiedList = getLastModified()
compileAll = compileAll or forceCompileAll()

filesCompiled = 0
try:
    for shader, modified in zip(compileList, lastModifiedList):
        if (compileAll or modified < int(os.path.getmtime(shader[0]))):
            if os.path.exists(shader[1]):
                os.remove(shader[1])
            output = subprocess.Popen([glslangValidator, "-V", shader[0], "-o", shader[1] ], stdout=subprocess.PIPE).communicate()[0]
            print(output.decode('utf-8'))
            filesCompiled = filesCompiled + 1
except FileNotFoundError:
    print("Path to glslangValidator is invalid!")

setLastModified("lastModified.temp", compileList)
setLastModified("lastModifiedForced.temp", forceFullCompilationList)

print("Files compiled: " + str(filesCompiled) + "/" + str(len(compileList)))