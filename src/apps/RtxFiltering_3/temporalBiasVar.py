import numpy as np
import matplotlib.pyplot as plt

def beta(i : int):
    if i == 1:
        return 1
    return 0.5 * np.sqrt(6 * i / ((i + 1) * (2*i + 1)))
    

def bias(n : int):
    sum = 0

    for i in range(n):
        sum += (1 - beta(i+1)) * (i + 1)
    
    return sum

def variance(n : int):
    betaSum = 0
    sum = 0
    for i in range(n):
        b = beta(i+1)
        betaSum += b
        sum += betaSum**2
    
    return sum / (n**2)

# for i in range(100):
#     b = bias(i + 1)
#     v = variance(i + 1)

#     print(str(i+1) + " Bias:" + str(b) + " Variance:" + str(v) + " Mse:" + str(b**2 + v))

expMovingAvg1 = 0
expMovingAvg2 = 0
expMovingErr = 0
weight = 0.95
errWeight = 0.4
freq = 0.1
last = 0
ref = []
data1 = []
data2 = []
for i in range(500):
    if (i < 100):
        a = 4*np.sin(freq * i) 
        r = a + (np.random.rand() - 0.5)
    elif (i >= 100):
        a = 4
        r = a + (np.random.rand() - 0.5)

    expMovingErr = errWeight * expMovingErr + (1 - errWeight) * (r - last)
    expMovingAvg1 = weight * expMovingAvg1 + (1 - weight) * r
    expMovingAvg2 = weight * expMovingAvg1 + (1 - weight) * r + expMovingErr
   
    ref.append(a)
    data1.append(expMovingAvg1)
    data2.append(expMovingAvg2)
    last = r
    print(str(4*np.sin(freq * i)) + " " + str(expMovingAvg1) + " " + str(expMovingAvg2) + " " + str((expMovingAvg1 - expMovingAvg2) / expMovingAvg2))

plt.plot(ref)
plt.plot(data1, label="No EC")
plt.plot(data2)
plt.legend()
plt.show()