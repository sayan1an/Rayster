import numpy as np
from sampleAnalayze import *

#plotHistogram(arr1)


def getGhWeights(maxOrder : int):
    ghWeightArray = np.zeros((maxOrder * (maxOrder + 1) // 2, 2))

    for i in range(1, maxOrder+1):
        x, w = np.polynomial.hermite.hermgauss(i)
        sortIdx = (-w).argsort()
        x = x[sortIdx]
        w = w[sortIdx]
        offset = i * (i - 1) // 2
        for j in range(i):    
            ghWeightArray[offset + j][0] = x[j]
            ghWeightArray[offset + j][1] = w[j] * np.exp(x[j]**2)
       
    return ghWeightArray

def refIntegral(mean, var_x, var_y, var_xy):
    m = np.arange(0, 1, 0.005)
    x, y = np.meshgrid(m, m)

    z = gauss2D(x, y, mean, var_x, var_y, var_xy)

    return np.mean(z)

ghWeights = getGhWeights(100)
refMean, refVar_x, refVar_y, refVar_xy, _, _, _, _ = computeReferenceMeanVar(arr1)
print(refIntegral(refMean, refVar_x, refVar_y, refVar_xy))

perFrameMeanVarList, _, _, _, _ = computePerFrameMeanVar(arr1, refMean, refVar_x, refVar_y, refVar_xy, False)
f = lambda x, y : gauss2D(x, y, refMean, refVar_x, refVar_y, refVar_xy)

# idea
# lower std should require lower order
# higher std should require higher order

def gaussHermitIntegral(mean_x, mean_y, var_x, var_y, weights, orderX, orderY):
    offsetX = orderX * (orderX - 1) // 2
    offsetY = orderY * (orderY - 1) // 2

    integral = 0
    functionEval = 0
    for i in range(orderX):
        x, wx = weights[offsetX + i]
        x_trans = np.sqrt(2 * var_x) * x + mean_x
        #x_limit_min = - mean_x / np.sqrt(2 * var_x)
        #x_limit_max =  (1.0 - mean_x) / np.sqrt(2 * var_x)
        for j in range(orderY):
            y, wy = weights[offsetY + j]
            y_trans = np.sqrt(2 * var_y) * y + mean_y

            #y_limit_min = - mean_y / np.sqrt(2 * var_y)
            #y_limit_max =  (1.0 - mean_y) / np.sqrt(2 * var_y)
            #print(str(x_trans) + " " + str(y_trans))

            #if x_trans >= x_limit_min and x_trans <= x_limit_max and y_trans >= y_limit_min and y_trans <= y_limit_max:
            if x_trans >= 0 and x_trans <= 1 and y_trans >= 0 and y_trans <= 1:
                integral = integral + wx * wy * f(x_trans, y_trans) #/ (np.exp(-x*x) * np.exp(-y*y))
                functionEval += 1


    #print(str(functionEval) + " " + str(functionEval / (orderX * orderY)))
    return integral * 2 * np.sqrt(var_x * var_y)

def testGhIntegral(perFrameMeanVarList, weights):
    
    for i in range(perFrameMeanVarList.shape[0]):
        meanVar = perFrameMeanVarList[i]
   
        # meanVar[0] = refMean[0]
        # meanVar[1] = refMean[1]
        # meanVar[2] = refVar_x
        # meanVar[3] = refVar_y
    
        print(gaussHermitIntegral(meanVar[0], meanVar[1], meanVar[2], meanVar[3], weights, 2, 2))

print(testGhIntegral(perFrameMeanVarList, ghWeights))
