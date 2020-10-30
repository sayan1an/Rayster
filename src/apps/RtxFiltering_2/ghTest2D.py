import numpy as np
import matplotlib.pyplot as plt

# https://en.wikipedia.org/wiki/Multivariate_normal_distribution
def gauss2D(x, y, mean_x, mean_y, std_x, std_y, var_xy):
    rho = var_xy / (std_x * std_y)
    rho_sq = 1.0 - rho * rho

    a = (x - mean_x) / std_x
    b = (y - mean_y) / std_y

    t = a * a + b * b - 2 * rho * a * b
    
    return np.exp(-0.5 * t / rho_sq) / (2.0 * np.pi * std_x * std_y * np.sqrt(rho_sq))

def box2D(x, y, mean_x, mean_y, std_x, std_y):
    x_lim_lo = mean_x - std_x
    x_lim_hi = mean_x + std_x
    y_lim_lo = mean_y - std_y
    y_lim_hi = mean_y + std_y

    a = np.logical_and(x > x_lim_lo, x < x_lim_hi)
    b = np.logical_and(y > y_lim_lo, y < y_lim_hi)

    return np.logical_and(a, b)

def fComposite(x, y, shadow = True):
    meanGauss_x = 0.5
    meanGauss_y = 0.5
    stdGauss_x = 0.05
    stdGauss_y = 0.05
    varGauss_xy = 0.00
    rayIntersected = box2D(x,y, 0.5, 0.5, 0.5, 0.5)

    shadowMean_x = 0.6
    shadowMean_y = 0.5
    shadowStd_x = 0.05
    shadowStd_y = 0.05

    shadowVal = 1 - box2D(x,y, shadowMean_x, shadowMean_y, shadowStd_x, shadowStd_y) * int(shadow)
    return  rayIntersected * gauss2D(x,y, meanGauss_x, meanGauss_y, stdGauss_x, stdGauss_y, varGauss_xy) * shadowVal, rayIntersected

def findRefMeanStd():
    e = np.arange(0,1, 0.0005)

    x, y = np.meshgrid(e, e)
    z, _ = fComposite(x,y, shadow = False) # Without shadow

    meanx = np.sum(z * x) / np.sum(z)
    meany = np.sum(z * y) / np.sum(z)

    stdx = np.sqrt(np.sum(z * (x - meanx)**2) / np.sum(z))
    stdy = np.sqrt(np.sum(z * (y - meany)**2) / np.sum(z))
   
    zS, _ = fComposite(x,y) # With Shadow
    return meanx, meany, stdx, stdy, np.mean(zS)

def contourPlot():
    e = np.arange(-.1, 1.1, 0.001)

    x, y = np.meshgrid(e, e)
    z, _ = fComposite(x,y)

    plt.contourf(x, y, z)
    plt.show()

def integral(order : int, meanx : float, meany : float, stdx : float, stdy : float, cutOffStd : float):
    p, w = np.polynomial.hermite.hermgauss(order)
    w *= np.exp(p**2)

    x, y = np.meshgrid(p, p)
    weights = np.dot(w.reshape(-1,1), w.reshape(1,-1))
    
    tx = np.sqrt(2) * x * stdx + meanx
    ty = np.sqrt(2) * y * stdy + meany

    pruneWeight = box2D(tx, ty, meanx, meany, cutOffStd * stdx, cutOffStd * stdy)
    sampleVal, sampleEvaluations = fComposite(tx, ty)
  
    return np.sum(pruneWeight * sampleVal * weights) * 2 * stdx * stdy, np.sum(pruneWeight * sampleEvaluations)

# def integralS(order : int, meanx : float, meany : float, stdx : float, stdy : float):
#     x, w = np.polynomial.hermite.hermgauss(order)
#     w *= np.exp(x**2)

#     maxStd = 30
#     sum = 0
#     sampleEvaluations = 0
#     for i in range(x.shape[0]):
#         a = np.sqrt(2) * x[i]
#         if np.abs(a) > maxStd:
#             continue
#         tx = a * stdx + meanx
#         for j in range(x.shape[0]):
#             b = np.sqrt(2) * x[j]
#             if np.abs(b) > maxStd:
#                 continue
#             ty = b * stdy + meany
#             sampleVal, sampleEvaluated = fComposite(tx, ty)
#             sum += w[i] * w[j] * sampleVal
#             sampleEvaluations += sampleEvaluated

#     return sum * 2 * stdx * stdy, sampleEvaluations

def testIntegral(order : int, refMeanx : float, refMeany : float, refStdx : float, refStdy : float, refIntegral : float, cutOffStd : float):
    x = np.arange(-1.5, 1.5, 0.05)
    y = np.arange(-0.4, 1, 0.05)

    xGrid, yGrid = np.meshgrid(x, y)

    meanx = refMeanx + xGrid * refStdx
    meany = refMeany + xGrid * refStdy

    stdx = (1 + yGrid) * refStdx
    stdy = (1 + yGrid) * refStdy

    z = np.zeros(xGrid.shape)
    s = np.zeros(xGrid.shape)
    
    for i in range(xGrid.shape[0]):
        for j in range(xGrid.shape[1]):
            val, nEvaluations = integral(order, meanx[i, j], meany[i, j], stdx[i, j], stdy[i, j], cutOffStd)
            z[i,j] = np.abs(val - refIntegral) / np.abs(refIntegral)
            s[i,j] = nEvaluations

    print("Mean error:" + str(np.mean(z)))
    print("Mean samples:" + str(np.mean(s)))

    # f, ax = plt.subplots(1,2)
    # i0 = ax[0].contourf(xGrid, yGrid, z, cmap = 'Spectral')
    # f.colorbar(i0, ax=ax[0])
    # i1 = ax[1].contourf(xGrid, yGrid, s, cmap = 'Spectral')
    # f.colorbar(i1, ax=ax[1])
    # plt.show()

def plotSampleDensityPerStd():
    sampleDensity = []
    stdCount = 2
    std = 1 / np.sqrt(2)
    for i in range(100):
        p, w = np.polynomial.hermite.hermgauss(i + 1)
        x, y = np.meshgrid(p, p)
        sampleDensity.append(np.sum(1.0 * (np.abs(x) < stdCount * std) * (np.abs(y) < stdCount * std)) / (4 * (stdCount**2)))
        #sampleDensity.append(np.sum(box2D(x, y, 0, 0, stdCount/np.sqrt(2), stdCount/np.sqrt(2))) / (4 * (stdCount**2)))        

    plt.plot(np.arange(100) + 1, sampleDensity)
    plt.show()
#plotSampleDensityPerStd()

contourPlot()
refMean_x, refMean_y, refStd_x, refStd_y, refIntegral = findRefMeanStd()
print(refMean_x, refMean_y, refStd_x, refStd_y, refIntegral)
testIntegral(3, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 2.5)
testIntegral(7, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 1.25)
testIntegral(11, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 1)
testIntegral(17, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 1)

# testIntegral(2, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 2.5)
# testIntegral(8, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 1.15)
# testIntegral(12, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 1)
# testIntegral(16, refMean_x, refMean_y, refStd_x, refStd_y, refIntegral, 0.85)
# for i in range(20):
#     print("Order" + str(i+1))
#     

