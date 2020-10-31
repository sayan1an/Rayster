import numpy as np
import matplotlib.pyplot as plt

# return value and number of ray-trace performed
def f(x:float):
    if x < 0.0:
        return 0, 0
    if x > 1.0:
        return 0, 0

    # if x < 0.25:
    #     return 2, 1
    # if x > 0.75:
    #     return 2, 1
    
    return 1, 1

# def f(x:float):
#     std = 0.5
#     mean = 0.5 
#     return np.exp(-(x-mean)**2/(2 * std * std)) / (std * np.sqrt(2 * np.pi))

def integral(order : int, std : float, mean : float):
    x, w = np.polynomial.hermite.hermgauss(order)
    w *= np.exp(x**2)

    sum = 0
    sampleEvaluations = 0
    for i in range(x.shape[0]):
        a = np.sqrt(2) * x[i]
        if np.abs(a) > 120:
            continue
        t = a * std + mean
        sampleVal, sampleEvaluated = f(t)
        sum += w[i] * sampleVal
        sampleEvaluations += sampleEvaluated

    return sum * np.sqrt(2) * std, sampleEvaluations

def testIntegral(mean : float):
    o = np.arange(1, 17, 0.2)
    std = np.arange(.1, 0.75, 0.05)

    oGrid, stdGrid = np.meshgrid(o, std)
    z = np.zeros(oGrid.shape)
    s = np.zeros(oGrid.shape)
    
    for i in range(oGrid.shape[0]):
        for j in range(oGrid.shape[1]):
            val, nEvaluations = integral(int(oGrid[i, j]), stdGrid[i, j], mean)
            z[i,j] = np.abs(val - 1)
            s[i,j] = nEvaluations

    f, ax = plt.subplots(1,2)
    i0 = ax[0].contourf(oGrid, stdGrid, z, cmap = 'Spectral')
    f.colorbar(i0, ax=ax[0])
    i1 = ax[1].contourf(oGrid, stdGrid, s, cmap = 'Spectral')
    f.colorbar(i1, ax=ax[1])
    plt.show()


def testIntegral2(order : int, refMean : float, refStd : float, refIntegral : float):
    x = np.arange(-1.5, 1.5, 0.05)
    y = np.arange(-0.5, 1, 0.05)

    xGrid, yGrid = np.meshgrid(x, y)

    mean = refMean + xGrid * refStd
    std = (1 + yGrid) * refStd

    z = np.zeros(xGrid.shape)
    s = np.zeros(xGrid.shape)
    
    for i in range(xGrid.shape[0]):
        for j in range(xGrid.shape[1]):
            val, nEvaluations = integral(order, std[i, j], mean[i, j])
            z[i,j] = np.abs(val - refIntegral) / np.abs(refIntegral)
            s[i,j] = nEvaluations

    print("Mean error:" + str(np.mean(z)))
    print("Mean samples:" + str(np.mean(s)))

    f, ax = plt.subplots(1,2)
    i0 = ax[0].contourf(xGrid, yGrid, z, cmap = 'Spectral')
    f.colorbar(i0, ax=ax[0])
    i1 = ax[1].contourf(xGrid, yGrid, s, cmap = 'Spectral')
    f.colorbar(i1, ax=ax[1])
    plt.show()

def findRefMeanStd():
    x = np.arange(0,1, 0.001)
    z = np.zeros(x.shape)
   
    for i in range(x.shape[0]):
        z[i], _ = f(x[i])

    refMean = np.sum(z*x) / np.sum(z)

    refStd = np.sqrt(np.sum(z*(x - refMean)**2) / np.sum(z))

    return refMean, refStd, np.mean(z)

refMean, refStd, refIntegral = findRefMeanStd()


#testIntegral(0.5)
#refStd = np.sqrt(1/12)
#refMean = 0.5
#print(integral(16, (1 + 0.15) * refStd, refMean - 0.5 * refStd))
testIntegral2(17, refMean, refStd, refIntegral)

# denisty = []
# for i in range(100):
#     x, w = np.polynomial.hermite.hermgauss(i+1)
#     points = 0
#     for j in range(i + 1):
#         t = np.sqrt(2) * 0.75 * x[j] + 0.85
#         #t = x[j]
#         if t < 1.00001 and t > -0.000001:
#             points += 1
#     print(str(i + 1) + " " + str(points))