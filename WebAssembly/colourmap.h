#ifndef COLOURMAP_H
#define COLOURMAP_H

#include <stdbool.h>

//modified ocean (sometimes with R and G channels swapped)
static const float ocean_b[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.04761904761904762, 0.09523809523809523, 0.1428571428571428, 0.1904761904761905, 0.2380952380952381, 0.2857142857142857, 0.3333333333333333, 0.3809523809523809, 0.4285714285714285, 0.4761904761904762, 0.5238095238095238, 0.5714285714285714, 0.6190476190476191, 0.6666666666666666, 0.7142857142857143, 0.7619047619047619, 0.8095238095238095, 0.8571428571428571, 0.9047619047619048, 0.9523809523809523, 1, 1};

static const float ocean_r[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.02380952380952381, 0.04761904761904762, 0.07142857142857142, 0.09523809523809523, 0.119047619047619, 0.1428571428571428, 0.1666666666666667, 0.1904761904761905, 0.2142857142857143, 0.2380952380952381, 0.2619047619047619, 0.2857142857142857, 0.3095238095238095, 0.3333333333333333, 0.3571428571428572, 0.3809523809523809, 0.4047619047619048, 0.4285714285714285, 0.4523809523809524, 0.4761904761904762, 0.5, 0.5238095238095238, 0.5476190476190477, 0.5714285714285714, 0.5952380952380952, 0.6190476190476191, 0.6428571428571429, 0.6666666666666666, 0.6904761904761905, 0.7142857142857143, 0.7380952380952381, 0.7619047619047619, 0.7857142857142857, 0.8095238095238095, 0.8333333333333334, 0.8571428571428571, 0.8809523809523809, 0.9047619047619048, 0.9285714285714286, 0.9523809523809523, 0.9761904761904762, 1, 1};

static const float ocean_g[] = { 0, 0.01587301587301587, 0.03174603174603174, 0.04761904761904762, 0.06349206349206349, 0.07936507936507936, 0.09523809523809523, 0.1111111111111111, 0.126984126984127, 0.1428571428571428, 0.1587301587301587, 0.1746031746031746, 0.1904761904761905, 0.2063492063492063, 0.2222222222222222, 0.2380952380952381, 0.253968253968254, 0.2698412698412698, 0.2857142857142857, 0.3015873015873016, 0.3174603174603174, 0.3333333333333333, 0.3492063492063492, 0.3650793650793651, 0.3809523809523809, 0.3968253968253968, 0.4126984126984127, 0.4285714285714285, 0.4444444444444444, 0.4603174603174603, 0.4761904761904762, 0.492063492063492, 0.5079365079365079, 0.5238095238095238, 0.5396825396825397, 0.5555555555555556, 0.5714285714285714, 0.5873015873015873, 0.6031746031746031, 0.6190476190476191, 0.6349206349206349, 0.6507936507936508, 0.6666666666666666, 0.6825396825396826, 0.6984126984126984, 0.7142857142857143, 0.7301587301587301, 0.746031746031746, 0.7619047619047619, 0.7777777777777778, 0.7936507936507936, 0.8095238095238095, 0.8253968253968254, 0.8412698412698413, 0.8571428571428571, 0.873015873015873, 0.8888888888888888, 0.9047619047619048, 0.9206349206349206, 0.9365079365079365, 0.9523809523809523, 0.9682539682539683, 0.9841269841269841, 1, 1};

//OpenCV 'hot'
static const float hot_r[] = { 0, 0.03968253968253968, 0.07936507936507936, 0.119047619047619, 0.1587301587301587, 0.1984126984126984, 0.2380952380952381, 0.2777777777777778, 0.3174603174603174, 0.3571428571428571, 0.3968253968253968, 0.4365079365079365, 0.4761904761904762, 0.5158730158730158, 0.5555555555555556, 0.5952380952380952, 0.6349206349206349, 0.6746031746031745, 0.7142857142857142, 0.753968253968254, 0.7936507936507936, 0.8333333333333333, 0.873015873015873, 0.9126984126984127, 0.9523809523809523, 0.992063492063492, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static const float hot_g[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.03174603174603163, 0.0714285714285714, 0.1111111111111112, 0.1507936507936507, 0.1904761904761905, 0.23015873015873, 0.2698412698412698, 0.3095238095238093, 0.3492063492063491, 0.3888888888888888, 0.4285714285714284, 0.4682539682539679, 0.5079365079365079, 0.5476190476190477, 0.5873015873015872, 0.6269841269841268, 0.6666666666666665, 0.7063492063492065, 0.746031746031746, 0.7857142857142856, 0.8253968253968254, 0.8650793650793651, 0.9047619047619047, 0.9444444444444442, 0.984126984126984, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static const float hot_b[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.04761904761904745, 0.1269841269841265, 0.2063492063492056, 0.2857142857142856, 0.3650793650793656, 0.4444444444444446, 0.5238095238095237, 0.6031746031746028, 0.6825396825396828, 0.7619047619047619, 0.8412698412698409, 0.92063492063492, 1, 1};

//OpenCV 'rainbow'
static const float rainbow_r[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.9365079365079367, 0.8571428571428572, 0.7777777777777777, 0.6984126984126986, 0.6190476190476191, 0.53968253968254, 0.4603174603174605, 0.3809523809523814, 0.3015873015873018, 0.2222222222222223, 0.1428571428571432, 0.06349206349206415, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.03174603174603208, 0.08465608465608465, 0.1375661375661377, 0.1904761904761907, 0.2433862433862437, 0.2962962962962963, 0.3492063492063493, 0.4021164021164023, 0.4550264550264553, 0.5079365079365079, 0.5608465608465609, 0.6137566137566139, 0.666666666666667, 0.666666666666667};

static const float rainbow_g[] = { 0, 0.03968253968253968, 0.07936507936507936, 0.119047619047619, 0.1587301587301587, 0.1984126984126984, 0.2380952380952381, 0.2777777777777778, 0.3174603174603174, 0.3571428571428571, 0.3968253968253968, 0.4365079365079365, 0.4761904761904762, 0.5158730158730158, 0.5555555555555556, 0.5952380952380952, 0.6349206349206349, 0.6746031746031745, 0.7142857142857142, 0.753968253968254, 0.7936507936507936, 0.8333333333333333, 0.873015873015873, 0.9126984126984127, 0.9523809523809523, 0.992063492063492, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0.9841269841269842, 0.9047619047619047, 0.8253968253968256, 0.7460317460317465, 0.666666666666667, 0.587301587301587, 0.5079365079365079, 0.4285714285714288, 0.3492063492063493, 0.2698412698412698, 0.1904761904761907, 0.1111111111111116, 0.03174603174603208, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static const float rainbow_b[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.01587301587301582, 0.09523809523809534, 0.1746031746031744, 0.2539682539682535, 0.333333333333333, 0.412698412698413, 0.4920634920634921, 0.5714285714285712, 0.6507936507936507, 0.7301587301587302, 0.8095238095238093, 0.8888888888888884, 0.9682539682539679, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

//CUBEHELIX
static const float cubehelix_r[] = {0.000,0.026,0.049,0.068,0.083,0.093,0.100,0.104,0.104,0.102,0.098,0.093,0.089,0.085,0.082,0.083,0.086,0.094,0.105,0.122,0.144,0.170,0.201,0.237,0.277,0.320,0.366,0.414,0.463,0.512,0.559,0.605,0.649,0.689,0.724,0.755,0.781,0.802,0.817,0.827,0.833,0.833,0.830,0.824,0.815,0.804,0.793,0.782,0.773,0.765,0.760,0.758,0.760,0.766,0.776,0.791,0.809,0.831,0.856,0.883,0.912,0.942,0.972,1.000,1} ;

static const float cubehelix_g[] = {0.000,0.009,0.019,0.031,0.044,0.059,0.077,0.097,0.118,0.142,0.167,0.194,0.221,0.249,0.277,0.305,0.332,0.357,0.380,0.402,0.420,0.437,0.450,0.461,0.470,0.475,0.479,0.481,0.481,0.479,0.478,0.476,0.474,0.474,0.474,0.477,0.481,0.488,0.498,0.510,0.525,0.543,0.563,0.586,0.611,0.638,0.665,0.694,0.723,0.753,0.781,0.809,0.835,0.859,0.882,0.902,0.920,0.937,0.951,0.963,0.973,0.983,0.992,1.000,1} ;

static const float cubehelix_b[] = {0.000,0.025,0.054,0.085,0.116,0.148,0.179,0.209,0.235,0.258,0.277,0.292,0.302,0.307,0.307,0.303,0.296,0.284,0.271,0.255,0.239,0.224,0.209,0.197,0.189,0.184,0.184,0.190,0.201,0.218,0.241,0.270,0.304,0.343,0.387,0.434,0.483,0.534,0.586,0.636,0.686,0.733,0.776,0.816,0.851,0.881,0.905,0.925,0.939,0.948,0.953,0.955,0.953,0.950,0.946,0.941,0.938,0.936,0.937,0.941,0.949,0.962,0.978,1.000,1} ;

//parula
static const float parula_r[] = { 0.204576, 0.208165, 0.2117, 0.212181, 0.207609, 0.194457, 0.167427, 0.118502, 0.0511985, 0.0085886, 0.00709481, 0.0190768, 0.0362076, 0.0528843, 0.0653143, 0.0739604, 0.0787571, 0.0787893, 0.0724571, 0.0597237, 0.0437642, 0.0309437, 0.0251738, 0.0235411, 0.0227747, 0.0235815, 0.0303852, 0.0463251, 0.0697708, 0.09723, 0.128166, 0.162073, 0.198902, 0.23884, 0.281803, 0.327492, 0.375036, 0.422848, 0.469472, 0.513943, 0.556177, 0.596425, 0.634975, 0.672085, 0.707964, 0.742828, 0.776912, 0.810266, 0.843041, 0.875396, 0.907463, 0.939098, 0.969258, 0.991574, 0.998999, 0.996271, 0.988949, 0.979757, 0.970376, 0.962949, 0.958952, 0.959711, 0.965962, 0.9763, 0.9763} ;

static const float parula_g[] = { 0.142819, 0.166671, 0.190533, 0.214932, 0.240212, 0.266548, 0.294764, 0.328212, 0.363891, 0.390823, 0.411619, 0.429558, 0.446054, 0.461715, 0.477016, 0.492269, 0.507967, 0.52457, 0.542927, 0.56308, 0.583546, 0.602553, 0.619153, 0.633646, 0.646387, 0.657829, 0.668426, 0.678399, 0.687838, 0.696876, 0.705478, 0.713695, 0.721488, 0.72863, 0.735003, 0.740444, 0.74458, 0.747332, 0.748802, 0.749182, 0.748808, 0.747798, 0.746225, 0.744177, 0.741877, 0.739207, 0.73633, 0.733458, 0.730557, 0.727931, 0.726019, 0.725797, 0.729887, 0.742703, 0.76234, 0.783435, 0.804323, 0.825178, 0.846433, 0.86904, 0.893684, 0.920937, 0.950953, 0.9831, 0.9831};

static const float parula_b[] = { 0.480724, 0.529967, 0.579227, 0.62934, 0.680296, 0.731943, 0.78423, 0.835452, 0.87135, 0.882576, 0.882419, 0.877599, 0.870526, 0.86232, 0.853487, 0.844659, 0.836393, 0.829597, 0.825372, 0.823623, 0.822194, 0.81813, 0.810459, 0.799478, 0.785946, 0.770551, 0.753758, 0.73589, 0.71698, 0.696943, 0.675861, 0.653835, 0.630767, 0.606849, 0.582356, 0.557806, 0.534045, 0.511812, 0.491451, 0.47277, 0.455373, 0.439063, 0.423691, 0.408947, 0.394647, 0.3807, 0.366914, 0.353226, 0.339345, 0.324955, 0.309588, 0.292269, 0.270892, 0.244523, 0.219562, 0.198987, 0.181208, 0.164819, 0.148727, 0.131984, 0.114105, 0.095429, 0.075859, 0.0538, 0.0538};

//inferno
static const float inferno_r[] = { 0, 0.00499461, 0.0182081, 0.0369849, 0.0586695, 0.0806064, 0.10014, 0.114615, 0.121375, 0.128341, 0.152172, 0.187589, 0.228711, 0.269657, 0.304545, 0.327494, 0.333881, 0.345888, 0.370324, 0.40282, 0.439008, 0.474519, 0.504984, 0.526034, 0.53337, 0.543817, 0.569075, 0.6037, 0.642246, 0.679267, 0.709318, 0.726952, 0.731136, 0.743616, 0.76521, 0.792406, 0.821691, 0.849554, 0.872483, 0.886965, 0.890619, 0.89707, 0.909438, 0.925329, 0.942346, 0.958094, 0.970175, 0.976194, 0.976471, 0.976471, 0.976471, 0.976471, 0.976471, 0.976471, 0.976471, 0.976471, 0.976655, 0.977767, 0.979604, 0.98184, 0.984148, 0.986201, 0.987672, 0.988235, 0.988235};

static const float inferno_g[] = { 0, 0.0019334, 0.00704829, 0.0143167, 0.0227108, 0.0312025, 0.0387639, 0.044367, 0.046984, 0.047435, 0.048759, 0.0507266, 0.0530112, 0.055286, 0.0572242, 0.0584991, 0.0590276, 0.0635007, 0.0726043, 0.0847108, 0.0981926, 0.111422, 0.122772, 0.130614, 0.133348, 0.137527, 0.14763, 0.16148, 0.176898, 0.191707, 0.203727, 0.210781, 0.213237, 0.22389, 0.242324, 0.26554, 0.29054, 0.314326, 0.333899, 0.346261, 0.350001, 0.364954, 0.393627, 0.430465, 0.469914, 0.506418, 0.534425, 0.548378, 0.55467, 0.577118, 0.611819, 0.653548, 0.69708, 0.737189, 0.76865, 0.786238, 0.791549, 0.811565, 0.844637, 0.884884, 0.926426, 0.963379, 0.989865, 1, 1};

static const float inferno_b[] = { 0.0156863, 0.0266422, 0.0556266, 0.0968144, 0.144381, 0.1925, 0.235348, 0.267099, 0.281929, 0.286993, 0.303322, 0.327589, 0.355766, 0.383821, 0.407726, 0.42345, 0.427419, 0.426712, 0.425275, 0.423364, 0.421235, 0.419146, 0.417354, 0.416116, 0.415671, 0.411283, 0.400675, 0.386132, 0.369943, 0.354394, 0.341773, 0.334366, 0.331903, 0.321554, 0.303647, 0.281094, 0.256809, 0.233703, 0.214689, 0.20268, 0.199211, 0.18719, 0.164139, 0.134524, 0.102811, 0.0734638, 0.0509489, 0.0397315, 0.0429211, 0.0576408, 0.0803955, 0.107759, 0.136304, 0.162606, 0.183236, 0.194769, 0.203074, 0.24533, 0.315149, 0.400115, 0.487814, 0.565827, 0.62174, 0.643137, 0.643137};

//magma
static const float magma_r[] = { 0, 0.00451126, 0.016446, 0.0334057, 0.0529918, 0.0728058, 0.090449, 0.103523, 0.109629, 0.1162, 0.138707, 0.172157, 0.210994, 0.249665, 0.282615, 0.304289, 0.310341, 0.322112, 0.346069, 0.377928, 0.413407, 0.448221, 0.478089, 0.498726, 0.505921, 0.516785, 0.543054, 0.579064, 0.619151, 0.657653, 0.688906, 0.707246, 0.711823, 0.726434, 0.751714, 0.783553, 0.817839, 0.850459, 0.877302, 0.894256, 0.898463, 0.904913, 0.917282, 0.933173, 0.95019, 0.965937, 0.978018, 0.984037, 0.984592, 0.985696, 0.987402, 0.989454, 0.991595, 0.993568, 0.995115, 0.99598, 0.995956, 0.995214, 0.993989, 0.992499, 0.99096, 0.989592, 0.988611, 0.988235, 0.988235};

static const float magma_g[] = { 0, 0.00257786, 0.00939772, 0.019089, 0.030281, 0.0416033, 0.0516852, 0.0591561, 0.0626454, 0.0629959, 0.0638786, 0.0651903, 0.0667134, 0.0682299, 0.069522, 0.070372, 0.0707923, 0.0752654, 0.084369, 0.0964755, 0.109957, 0.123187, 0.134537, 0.142379, 0.145111, 0.148662, 0.15725, 0.169023, 0.182128, 0.194715, 0.204933, 0.210928, 0.212858, 0.220772, 0.234466, 0.251712, 0.270283, 0.287953, 0.302493, 0.311676, 0.314784, 0.33091, 0.361832, 0.401559, 0.444101, 0.483469, 0.513672, 0.52872, 0.534877, 0.556589, 0.590152, 0.630513, 0.672618, 0.711412, 0.741841, 0.758853, 0.764405, 0.786274, 0.822408, 0.866382, 0.91177, 0.952146, 0.981083, 0.992157, 0.992157};

static const float magma_b[] = { 0.0156863, 0.0259977, 0.0532772, 0.0920422, 0.13681, 0.182099, 0.222427, 0.25231, 0.266268, 0.273564, 0.297837, 0.33391, 0.375794, 0.417498, 0.453032, 0.476406, 0.482417, 0.48383, 0.486705, 0.490528, 0.494785, 0.498963, 0.502547, 0.505024, 0.505877, 0.504415, 0.500879, 0.496031, 0.490635, 0.485452, 0.481245, 0.478776, 0.477506, 0.470809, 0.459222, 0.44463, 0.428915, 0.413965, 0.401661, 0.393891, 0.392099, 0.39122, 0.389533, 0.387366, 0.385045, 0.382898, 0.381251, 0.38043, 0.383912, 0.397896, 0.419513, 0.445508, 0.472626, 0.497613, 0.517211, 0.528168, 0.532848, 0.553605, 0.587903, 0.629641, 0.67272, 0.711043, 0.738509, 0.74902, 0.74902};

//plasma
static const float plasma_r[] = { 0.0509804, 0.0609696, 0.0873966, 0.12495, 0.168319, 0.212193, 0.25126, 0.28021, 0.293731, 0.300388, 0.322454, 0.355248, 0.393324, 0.431237, 0.46354, 0.48479, 0.490658, 0.500781, 0.521384, 0.548783, 0.579295, 0.609235, 0.634921, 0.652669, 0.658849, 0.666162, 0.683843, 0.70808, 0.735062, 0.760977, 0.782012, 0.794357, 0.797172, 0.805086, 0.81878, 0.836026, 0.854597, 0.872266, 0.886806, 0.89599, 0.898405, 0.903976, 0.914658, 0.928382, 0.943078, 0.956678, 0.967112, 0.97231, 0.973012, 0.974852, 0.977696, 0.981117, 0.984685, 0.987973, 0.990552, 0.991993, 0.991359, 0.98654, 0.978579, 0.968889, 0.958889, 0.949993, 0.943616, 0.941176, 0.941176};

static const float plasma_g[] = { 0.0313726, 0.030567, 0.0284358, 0.0254072, 0.0219097, 0.0183715, 0.0152209, 0.0128863, 0.0117959, 0.0117647, 0.0117647, 0.0117647, 0.0117647, 0.0117647, 0.0117647, 0.0117647, 0.0120976, 0.0193959, 0.0342492, 0.0540019, 0.0759985, 0.0975835, 0.116101, 0.128897, 0.13336, 0.140881, 0.159067, 0.183997, 0.21175, 0.238405, 0.260042, 0.272739, 0.276066, 0.287329, 0.306816, 0.331358, 0.357787, 0.382931, 0.403623, 0.416692, 0.420397, 0.432418, 0.455469, 0.485084, 0.516797, 0.546144, 0.568659, 0.579876, 0.584746, 0.602042, 0.628778, 0.66093, 0.694471, 0.725375, 0.749616, 0.763167, 0.76802, 0.788035, 0.821108, 0.861355, 0.902896, 0.93985, 0.966335, 0.976471, 0.976471};

static const float plasma_b[] = { 0.529412, 0.533601, 0.544683, 0.560431, 0.578618, 0.597017, 0.6134, 0.62554, 0.631211, 0.63225, 0.63534, 0.639931, 0.645261, 0.650569, 0.655092, 0.658067, 0.65863, 0.654393, 0.645768, 0.634299, 0.621526, 0.608993, 0.598241, 0.590811, 0.588214, 0.582155, 0.567505, 0.547423, 0.525066, 0.503594, 0.486164, 0.475936, 0.473332, 0.464809, 0.450062, 0.431489, 0.41149, 0.392461, 0.376803, 0.366913, 0.364167, 0.355958, 0.340216, 0.319991, 0.298333, 0.278291, 0.262915, 0.255254, 0.252586, 0.243386, 0.229165, 0.212062, 0.194222, 0.177783, 0.164889, 0.157681, 0.156433, 0.153839, 0.149551, 0.144334, 0.138949, 0.134159, 0.130726, 0.129412, 0.129412};

//viridis
static const float viridis_r[] = { 0.266667, 0.26715, 0.268429, 0.270246, 0.272344, 0.274467, 0.276358, 0.277758, 0.278413, 0.276926, 0.271631, 0.26376, 0.254622, 0.245523, 0.23777, 0.23267, 0.231211, 0.22768, 0.220493, 0.210935, 0.200292, 0.189847, 0.180887, 0.174696, 0.172541, 0.170243, 0.164686, 0.157068, 0.148588, 0.140444, 0.133833, 0.129953, 0.129664, 0.13149, 0.134651, 0.13863, 0.142916, 0.146994, 0.150349, 0.152468, 0.153961, 0.169501, 0.199298, 0.23758, 0.278576, 0.316512, 0.345617, 0.360117, 0.36801, 0.396713, 0.441085, 0.494444, 0.550107, 0.601395, 0.641624, 0.664113, 0.67176, 0.702525, 0.753358, 0.81522, 0.87907, 0.93587, 0.976578, 0.992157, 0.992157};

static const float viridis_g[] = { 0.00392157, 0.0108496, 0.0291779, 0.0552232, 0.0853018, 0.11573, 0.142825, 0.162903, 0.172281, 0.177189, 0.193518, 0.217785, 0.245962, 0.274017, 0.297922, 0.313646, 0.317991, 0.325524, 0.340857, 0.361247, 0.383953, 0.406234, 0.425349, 0.438557, 0.44316, 0.449637, 0.465297, 0.486765, 0.510663, 0.533616, 0.552247, 0.563181, 0.565926, 0.574753, 0.590027, 0.609263, 0.629977, 0.649685, 0.665903, 0.676146, 0.678951, 0.686867, 0.702047, 0.72155, 0.742434, 0.76176, 0.776587, 0.783974, 0.786166, 0.793526, 0.804904, 0.818585, 0.832858, 0.846009, 0.856324, 0.86209, 0.86342, 0.867497, 0.874234, 0.882433, 0.890895, 0.898423, 0.903818, 0.905882, 0.905882};

static const float viridis_b[] = { 0.329412, 0.335534, 0.351731, 0.374748, 0.401329, 0.42822, 0.452164, 0.469907, 0.478195, 0.480563, 0.488066, 0.499216, 0.512162, 0.525052, 0.536035, 0.54326, 0.54513, 0.545837, 0.547274, 0.549186, 0.551314, 0.553403, 0.555195, 0.556433, 0.556862, 0.556653, 0.556148, 0.555455, 0.554685, 0.553944, 0.553343, 0.55299, 0.552436, 0.548784, 0.542464, 0.534504, 0.525933, 0.517778, 0.511067, 0.506828, 0.505305, 0.496509, 0.479643, 0.457973, 0.434768, 0.413295, 0.396821, 0.388613, 0.383696, 0.365665, 0.33779, 0.30427, 0.269302, 0.237083, 0.211811, 0.197683, 0.195281, 0.190462, 0.1825, 0.172811, 0.16281, 0.153914, 0.147538, 0.145098, 0.145098};

static const float haxby_r[] = { 0.1450980392156863,0.1469654528478058,0.1488328664799253,0.1507002801120448,0.1525676937441643,0.1544351073762839,0.1563025210084034,0.1612200435729848,0.1674447556800498,0.1736694677871148,0.1798941798941799,0.186118892001245,0.19234360410831,0.2100217864923747,0.244880174291939,0.2797385620915033,0.3145969498910676,0.3494553376906318,0.3843137254901962,0.4176781823840648,0.4375972611266729,0.4575163398692811,0.4774354186118892,0.4973544973544974,0.5172735760971056,0.5371926548397137,0.5745409274821039,0.6162464985994398,0.6579520697167756,0.6996576408341115,0.7413632119514472,0.7830687830687831,0.8148148148148148,0.8366013071895425,0.8583877995642701,0.8801742919389979,0.9019607843137255,0.9237472766884532,0.9430438842203548,0.9523809523809524,0.96171802054155,0.9710550887021475,0.9803921568627451,0.9897292250233427,0.9990662931839402,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };

static const float haxby_g[] = { 0.2235294117647059,0.2671023965141612,0.3106753812636166,0.3542483660130719,0.3978213507625272,0.4413943355119826,0.4849673202614379,0.5254901960784314,0.5647058823529412,0.6039215686274509,0.6431372549019608,0.6823529411764706,0.7215686274509804,0.7563025210084033,0.7843137254901961,0.8123249299719888,0.8403361344537815,0.8683473389355743,0.896358543417367,0.9216308745720511,0.9222533457827575,0.9228758169934641,0.9234982882041706,0.924120759414877,0.9247432306255835,0.9253657018362901,0.9349517584811702,0.9467787114845938,0.9586056644880174,0.9704326174914411,0.9822595704948647,0.9940865234982882,0.9940865234982882,0.9822595704948647,0.9704326174914411,0.9586056644880173,0.9467787114845938,0.9349517584811702,0.9196389666977901,0.8903828197945844,0.8611266728913787,0.831870525988173,0.8026143790849672,0.7733582321817616,0.7441020852785559,0.7254901960784313,0.7080610021786493,0.6906318082788672,0.6732026143790849,0.6557734204793029,0.6383442265795207,0.6407096171802054,0.6562713974478681,0.6718331777155307,0.6873949579831934,0.7029567382508559,0.7185185185185186,0.7422969187675071,0.7852474323062559,0.8281979458450048,0.8711484593837536,0.9140989729225023,0.9570494864612512,1,1 };

static const float haxby_b[] = { 0.6862745098039216,0.7335823218176158,0.7808901338313103,0.8281979458450047,0.875505757858699,0.9228135698723934,0.9701213818860878,0.9860566448801743,0.9885465297230004,0.9910364145658264,0.9935262994086523,0.9960161842514784,0.9985060690943044,1,1,1,1,1,1,0.9949579831932772,0.9445378151260503,0.8941176470588236,0.8436974789915966,0.7932773109243697,0.7428571428571428,0.6924369747899159,0.6763772175536882,0.6689075630252102,0.661437908496732,0.653968253968254,0.6464985994397759,0.6390289449112979,0.6225334578275755,0.5970121381886088,0.5714908185496421,0.5459694989106754,0.5204481792717086,0.4949268596327419,0.4702769996887644,0.4491129785247432,0.4279489573607221,0.4067849361967009,0.3856209150326798,0.3644568938686586,0.3432928727046375,0.3305322128851541,0.3187052598817304,0.3068783068783069,0.2950513538748833,0.2832244008714597,0.2713974478680361,0.2909430438842204,0.3314036725801432,0.3718643012760661,0.4123249299719889,0.4527855586679118,0.4932461873638346,0.5443510737628388,0.6202925614690323,0.6962340491752258,0.7721755368814194,0.8481170245876131,0.9240585122938065,1,1 };

void apply_colourmap(unsigned char* canvas, int w, int h, const unsigned char* luma, int stride_luma, const unsigned char* alpha, int stride_alpha, bool invert, const float* r, const float* g, const float* b) ;

void apply_greyscale(unsigned char* canvas, int w, int h, const unsigned char* luma, int stride_luma, const unsigned char* alpha, int stride_alpha, bool invert) ;

void apply_yuv(unsigned char* canvas,  const unsigned char* _y, const unsigned char* _u, const unsigned char* _v, int w, int h, int stride, const unsigned char* alpha);

#endif
