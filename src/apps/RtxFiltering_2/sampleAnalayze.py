import numpy as np
import matplotlib.pyplot as plt

arr1 = np.load("D:/projects/mcSamples_1.npy")

print(arr1.shape)

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
    rMean, rVar_x, rVar_y, rVar_xy, _, _, _, _ = computeReferenceMeanVar(arr1)
    xGrid, yGrid = np.meshgrid(edges, edges)
    zGrid = gauss2D(xGrid, yGrid, rMean, rVar_x, rVar_y, rVar_xy)
   
    f, ax = plt.subplots(1,2)
    
    ax[0].hist2d(x=samples[:index2, 0], y=samples[:index2, 1], bins=[edges, edges], weights=samples[:index2, 2], density=True)
    ax[1].contourf(xGrid, yGrid, zGrid)
    plt.show()


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

# compute reference mean, var_x, var_y, var_xy
def computeReferenceMeanVar(rawData):
    refMean = np.zeros(2)
    refVar_x = 0.0
    refVar_y = 0.0
    refVar_xy = 0.0

    refMean_shader = np.zeros(2)
    refVar_x_shader = 0
    refVar_y_shader = 0
    refVar_xy_shader = 0
    
    refWeight = 0.0
    index = 0
    while (index < rawData.shape[0]):
        nSamples = int(rawData[index,1])
        # read header
        approxMean_shader = rawData[index + 1, :-2]
        approxVar_x_shader = rawData[index + 2, 0]
        approxVar_y_shader = rawData[index + 2, 1]
        approxVar_xy_shader = rawData[index + 2, 3]

        index = index + int(rawData[index,0]) # Add header size
        for i in range(nSamples):
            sample = rawData[index + i]
            refMean = refMean + sample[2] * sample[:-2]
            
            refWeight = refWeight + sample[2]
        
        index = index + nSamples

    refMean = refMean / refWeight

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

    return refMean, refVar_x, refVar_y, refVar_xy, approxMean_shader, approxVar_x_shader, approxVar_y_shader, approxVar_xy_shader

def computePerFrameMeanVar(rawData, refMean, refVar_x, refVar_y, refVar_xy, computeErr : bool = True):
    errList = []
    movingMean = np.ones(2) * 0.5
    movingVar_x = 0.0
    movingVar_y = 0.0
    movingVar_xy = 0.0
    movingWeight = 0.0
    
    index = 0
    while (index < rawData.shape[0]):
        nSamples = int(rawData[index,1])
        mean_s = rawData[index, 2:]
        var_x_s = rawData[index + 1, 2]
        var_y_s = rawData[index + 1, 3]
        var_xy_s = rawData[index + 2, 2]

        index = index + int(rawData[index,0]) # Add header size

        weight = 0
        mean = np.zeros(2)
        var_x = 0
        var_y = 0
        var_xy = 0
        # https://stats.stackexchange.com/questions/26123/efficient-method-technique-to-update-covariance-matrix
        for i in range(nSamples):
            sample = rawData[index + i]
            
            mean = mean + sample[2] * (sample[:-2] - mean) / (weight + sample[2])
            var_x = weight * var_x / (weight + sample[2]) + weight * (sample[0] - mean[0])**2 / ((weight + sample[2])**2)
            var_y = weight * var_y / (weight + sample[2]) + weight * (sample[1] - mean[1])**2 / ((weight + sample[2])**2)
            var_xy = weight * var_xy / (weight + sample[2]) + weight * (sample[1] - mean[1]) * (sample[0] - mean[0])  / ((weight + sample[2])**2)
            
            weight = weight + sample[2]
        
        if np.sum(np.abs(mean - mean_s)) > 1e-6:
            print("Error computing mean:" + str(np.sum(np.abs(mean - mean_s))))
        if np.abs(var_x - var_x_s) > 1e-7:
            print("Error computing var x")
        if np.abs(var_y - var_y_s) > 1e-7:
            print("Error computing var y")
        if np.abs(var_xy - var_xy_s) > 1e-7:
            print("Error computing var xy")

        oldMovingMean = movingMean.copy()
        movingMean = movingMean + weight * (mean - movingMean) / (weight + movingWeight)
        movingVar_x = weight * var_x + movingWeight * movingVar_x + weight * (mean[0] - movingMean[0])**2 + movingWeight * (oldMovingMean[0] - movingMean[0])**2
        movingVar_x = movingVar_x / (weight + movingWeight)

        movingVar_y = weight * var_y + movingWeight * movingVar_y + weight * (mean[1] - movingMean[1])**2 + movingWeight * (oldMovingMean[1] - movingMean[1])**2
        movingVar_y = movingVar_y / (weight + movingWeight)

        movingVar_xy = weight * var_xy + movingWeight * movingVar_xy + weight * (mean[0] - movingMean[0])*(mean[1] - movingMean[1]) + movingWeight * (oldMovingMean[0] - movingMean[0])*(oldMovingMean[1] - movingMean[1])
        movingVar_xy = movingVar_xy / (weight + movingWeight)
        
        movingWeight = 0.9 * movingWeight + 0.1 * weight

        if computeErr:
            errMean_x = movingMean[0] - refMean[0]
            errMean_y = movingMean[1] - refMean[1]
            errStd_x = np.sqrt(movingVar_x) - np.sqrt(refVar_x)
            errStd_y = np.sqrt(movingVar_y) - np.sqrt(refVar_y)
            errVar_xy = movingVar_xy - refVar_xy
        else:
            errMean_x = movingMean[0]
            errMean_y = movingMean[1]
            errStd_x = movingVar_x
            errStd_y = movingVar_y
            errVar_xy = movingVar_xy
        
        errList.append([errMean_x, errMean_y, errStd_x, errStd_y, errVar_xy])
        
        index = index + nSamples

        
    return np.abs(np.array(errList)), movingMean, movingVar_x, movingVar_y, movingVar_xy

# refMean, refVar_x, refVar_y, refVar_xy, approxMean_s, approxVar_x_s, approxVar_y_s, approxVar_xy_s = computeReferenceMeanVar(arr1)

# err, approxMean, approxVar_x, approxVar_y, approxVar_xy = computePerFrameMeanVar(arr1, refMean, refVar_x, refVar_y, refVar_xy)

# print("Reference Mean " + str(refMean) + " " + str(approxMean_s) + " " + str(approxMean))
# print("Reference Std X " + str(np.sqrt(refVar_x)) + " " + str(np.sqrt(approxVar_x_s)) + " " + str(np.sqrt(approxVar_x)))
# print("Reference Std Y " + str(np.sqrt(refVar_y)) + " " + str(np.sqrt(approxVar_y_s)) + " " + str(np.sqrt(approxVar_y)))
# print("Reference Var XY " + str(refVar_xy) + " " + str(approxVar_xy_s) + " " + str(approxVar_xy))

# f, (ax1, ax2, ax3) = plt.subplots(3)
# ax1.plot(err[:, 0], label="mean x :" + str(np.mean(err[3:, 0])))
# ax1.plot(err[:, 1], label="mean y :" + str(np.mean(err[3:, 1])))
# ax1.legend()
# ax2.plot(err[:, 2], label="std x :" + str(np.mean(err[3:, 2])))
# ax2.plot(err[:, 3], label="std y :" + str(np.mean(err[3:, 3])))
# ax2.legend()
# ax3.plot(err[:, 4], label="var xy :" + str(np.mean(err[3:, 4])))
# ax3.legend()
# plt.show()

# plotHistogram(arr1)


