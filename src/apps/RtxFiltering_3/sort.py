import numpy as np
import random
import matplotlib.pyplot as plt

arr1 = np.load("mcSamples_3.npy")

print(arr1.shape)

# https://en.wikipedia.org/wiki/Multivariate_normal_distribution
def gauss2D(x, y, mean, var_x, var_y, var_xy):
    std_x = np.sqrt(var_x)
    std_y = np.sqrt(var_y)
    
    rho = var_xy / (std_x * std_y)
    rho_sq = 1.0 - rho * rho

    a = (x - mean[0]) / std_x
    b = (y - mean[1]) / std_y

    t = a * a + b * b - 2 * rho * a * b
    
    return np.exp(-0.5 * t / rho_sq) / (2.0 * np.pi * std_x * std_y * np.sqrt(rho_sq))

# compute reference mean, var_x, var_y, var_xy, mcmcVal
def computeReferenceMeanVar(rawData):
    refMean = np.zeros(2)
    refVar_x = 0.0
    refVar_y = 0.0
    refVar_xy = 0.0
 
    refWeight = 0.0
    index = 0
    totalSamples = 0
    while (index < rawData.shape[0]):
        nSamples = int(rawData[index,1])
        
        index = index + int(rawData[index,0]) # Add header size
        for i in range(nSamples):
            sample = rawData[index + i]
            refMean = refMean + sample[2] * sample[:-2]
            
            refWeight = refWeight + sample[2]
            totalSamples += 1
        index = index + nSamples

    refMean = refMean / refWeight

    mcmcRef = refWeight / totalSamples

    refWeight = 0.0
    index = 0
    while (index < rawData.shape[0]):
        nSamples = int(rawData[index,1])
        index = index + int(rawData[index,0]) # Add header size
        for i in range(nSamples):
            sample = rawData[index + i]
            refVar_x = refVar_x + sample[2] * (sample[0] - refMean[0])**2
            refVar_y = refVar_y + sample[2] * (sample[1] - refMean[1])**2
            refVar_xy = refVar_xy + sample[2] * (sample[0] - refMean[0])*(sample[1] - refMean[1])
            
            refWeight = refWeight + sample[2]
        
        index = index + nSamples

    refVar_x = refVar_x / refWeight
    refVar_y = refVar_y / refWeight
    refVar_xy = refVar_xy / refWeight
    
    return refMean, refVar_x, refVar_y, refVar_xy, mcmcRef

# plot 2D histogram of the data
def plotHistogram(rawData):
    samples = np.zeros((rawData.shape[0], 3))
    index = 0
    index2 = 0
    while (index < rawData.shape[0]):
        nSamples = int(rawData[index,1])
        index = index + int(rawData[index,0]) # Add header size
        for i in range(nSamples):
            sample = rawData[index + i]
            samples[index2] = sample[:-1]
            index2 = index2 + 1
        
        index = index + nSamples

    edges = np.arange(0, 1, 0.01)
    rMean, rVar_x, rVar_y, rVar_xy, _ = computeReferenceMeanVar(arr1)
    xGrid, yGrid = np.meshgrid(edges, edges)
    zGrid = gauss2D(xGrid, yGrid, rMean, rVar_x, rVar_y, rVar_xy)
   
    f, ax = plt.subplots(1,2)
    
    ax[0].hist2d(x=samples[:index2, 0], y=samples[:index2, 1], bins=[edges, edges], weights=samples[:index2, 2], density=True)
    ax[1].contourf(xGrid, yGrid, zGrid)
    plt.show()

MAX_SPP = 8
MAX_NEW_SAMPLE_PER_FRAME = 4
DECAY_RATE = 0.9
SUBSAMPLE = 3
X_DELTA = 0.001*1
Y_DELTA = 0.005*1

global_sample_buf = np.zeros((MAX_SPP, 4))

def mcmcPassSimple(gSample, mcmcVal):
    s_sample_1 = np.ones((MAX_SPP, 4))
    
    s_sample_1[:,:-1] = gSample[:MAX_SPP, :-1]

    nNewSamples = 0
    mcmcIter = mcmcVal.shape[0]
    for i in range(mcmcIter):
        if (nNewSamples < MAX_NEW_SAMPLE_PER_FRAME) and ((random.randint(0, 0xffffffff) & SUBSAMPLE) == 0):
            s_sample_1[nNewSamples, :-1] = mcmcVal[i]
            nNewSamples += 1
  
    gSample[:] = s_sample_1[:]

def mcmcPass(gSample, mcmcVal):
    s_sample_1 = np.zeros((MAX_SPP + MAX_NEW_SAMPLE_PER_FRAME, 4))
    s_sample_2 = np.zeros((MAX_SPP + MAX_NEW_SAMPLE_PER_FRAME, 4))
    s_flag = np.zeros(MAX_SPP + MAX_NEW_SAMPLE_PER_FRAME)
    
    # do whether or not a pixel is valid
    for i in range(MAX_SPP):
        s = gSample[i]
        s_sample_2[i, :-2] = s[0:2]
        s_sample_2[i, 2:] = s[2:] * DECAY_RATE

    nNewSamples = 0
    mcmcIter = mcmcVal.shape[0]
    for i in range(mcmcIter):
        if (nNewSamples < MAX_NEW_SAMPLE_PER_FRAME) and ((random.randint(0, 0xffffffff) & SUBSAMPLE) == 0):
            s_sample_1[nNewSamples, :-1] = mcmcVal[i]
            nNewSamples += 1
    
    for i in range(nNewSamples):
        querySample = s_sample_1[i, :-1].copy()
        s_sample_1[i] = np.zeros(4)
        scale = 1 + (1 - np.clip(querySample[2], 0, 1)) * 10
       
        for j in range(MAX_SPP + i + 1):
            element = s_sample_2[j]
            updatePosition = np.abs(element[3]) < 0.001
            update = updatePosition or ((np.abs(element[0] - querySample[0]) < X_DELTA * scale) and (np.abs(element[1] - querySample[1]) < Y_DELTA * scale))

            if updatePosition:
                s_sample_2[j,:-2] = querySample[:-1]
                
            if update: 
                s_sample_2[j, 2] = s_sample_2[j, 3] * s_sample_2[j, 2] + querySample[2] #mcmc value
                s_sample_2[j, 3] += 1 # sample weight
                s_sample_2[j, 2] /= s_sample_2[j, 3]
                s_flag[j] = 1  
            if update:
                break 
    
    nNewSamples = 0
    nOldSamples = 0
    for i in range(MAX_SPP + MAX_NEW_SAMPLE_PER_FRAME):
        if s_flag[i] == 1:
            s_sample_1[MAX_SPP + MAX_NEW_SAMPLE_PER_FRAME - nNewSamples - 1] = s_sample_2[i]
            nNewSamples += 1
        
        if (s_flag[i] != 1) and (s_sample_2[i, 3] > 0.001):
            s_sample_1[nOldSamples] = s_sample_2[i]
            nOldSamples += 1
  
    # semi-importance sample
    # Just do a few pass of bubble sort
    for j in range(3):
        i = j
        while (i + 1) < nOldSamples:
            if s_sample_1[nOldSamples - i - 1,3] > s_sample_1[nOldSamples - i - 2,3]:
                temp = s_sample_1[nOldSamples - i - 1].copy()
                s_sample_1[nOldSamples - i - 1] = s_sample_1[nOldSamples - i - 2].copy()
                s_sample_1[nOldSamples - i - 2] = temp
            i+=1
   
    offset = np.min([MAX_SPP - nNewSamples, nOldSamples])
    for i in range(nNewSamples):
        s_sample_1[offset + i] = s_sample_1[MAX_SPP + MAX_NEW_SAMPLE_PER_FRAME - i - 1]
    
    gSample[:] = s_sample_1[:MAX_SPP]

#plotHistogram(arr1)
def biasVar(data, ref):
    mean = np.mean(data)

    bias = mean - ref
    var = np.mean((np.array(data) - mean)**2)

    print("Bias:" + str(np.abs(bias)) + " Variance:" + str(var) + " MSE:" + str(np.abs(bias)**2 + var))

def renderLoop(rawData):
    rawDataCpy = rawData.copy()
    index = 0
    loop = 0
    smart = []
    raw = [] 
    while (index < rawData.shape[0]):
        nSamples = int(rawData[index,1])
        index = index + int(rawData[index,0]) # Add header size
        mcmcSamples = rawData[index:index+nSamples, 0:3]
        mcmcPass(global_sample_buf, mcmcSamples.copy())
        smart.append(np.sum(global_sample_buf[:,2] * global_sample_buf[:,3]) / np.max([np.sum(global_sample_buf[:,3]), 0.00001]))
        raw.append(np.sum(mcmcSamples[:,2])/mcmcSamples.shape[0])
        print(str(np.sum(global_sample_buf[:,2] * global_sample_buf[:,3]) / np.sum(global_sample_buf[:,3])) + " " + str(np.sum(mcmcSamples[:,2])/mcmcSamples.shape[0]))
        #print(global_sample_buf)
        # if loop > 2:
        #     break
        loop += 1
        index = index + nSamples
    
    simple = []
    index = 0
    global_sample_buf[:]= 0
    while (index < rawDataCpy.shape[0]):
        nSamples = int(rawDataCpy[index,1])
        index = index + int(rawDataCpy[index,0]) # Add header size
        mcmcSamples = rawDataCpy[index:index+nSamples, 0:3]
        mcmcPassSimple(global_sample_buf, mcmcSamples.copy())
        simple.append(np.sum(global_sample_buf[:,2] * global_sample_buf[:,3]) / np.sum(global_sample_buf[:,3]))
       
        index = index + nSamples
    
    _, _, _, _, ref = computeReferenceMeanVar(rawData)
    biasVar(raw, ref)
    biasVar(simple, ref)
    biasVar(smart, ref)
    
    plt.plot(simple, label="Simple")
    plt.plot(smart, label="Smart")
    plt.plot(raw, label="Raw")
    plt.legend()
    plt.show()

    
renderLoop(arr1)
