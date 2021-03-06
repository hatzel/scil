/*
* Copyright (c) 2011 Nathanael Hübbe
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*/

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <netcdf.h>

#include <scil.h>

#include <time.h>

const int kRandSeed = 6;
const int kDimensionCount = 3;
const int kDimensionSize = 129;

typedef enum {
	kNoErr = 0,
	kErrParam,
	kErrCantOpenForWrite,
	kErrCantOpenForRead,
	kErrCantCloseFile,
	kErrNoMem,
	kErrInternal
} ErrorCode;

typedef int32_t dataType;

#define allocate(type, name, count) type* name = (type*)malloc(count * sizeof(type))

/**
* Produce a random number in the range ]-1, 1[ or [+0, 1[ where all the mantissa bits are random, even for small numbers and even though the distribution is flat.
*/
double superRand(int allowNegative) {
	int64_t bits = 0;
	int bitCount = 0;
	for(; bits < 0x0010000000000000; bitCount++) bits = (bits << 1) | ((rand() >> 16) & 1);
	double randValue = (double)bits;
	for(; bitCount > 0; bitCount--) randValue /= 2;
	if(allowNegative) {
		if(rand() & 0x10000) randValue *= -1;
	}
	return randValue;
}

ErrorCode makeUpData(long dimCount, size_t* dimSizes, double* data, int allowNegative, double bias, double correlation, int correlDim) {
	allocate(size_t, curCoords, dimCount);
	if(!curCoords) return kErrNoMem;
	long i;
	size_t variableSize = 1;
	for(i = 0; i < dimCount; i++) {
		curCoords[i] = 0;
		variableSize *= dimSizes[i];
	}

	//make up some data
	for(i = 0; (size_t)i < variableSize; i++) {
		double correlValue;
		if(correlDim < 0 || correlDim >= dimCount) {
			int j, neighbourCount = 0, offset = 1;
			double sum = 0;
			for(j = dimCount-1; j >= 0; j--) {
				if(curCoords[j]) {
					sum += data[i - offset];
					neighbourCount++;
				}
				offset *= dimSizes[j];
			}
			correlValue = (neighbourCount) ? (sum/neighbourCount) : bias;
		} else {
			if(curCoords[correlDim]) {
				int j, offset = 1;
				for(j = dimCount-1; j > correlDim; j--) offset *= dimSizes[j];
				correlValue = data[i - offset];
			} else {
				correlValue = bias;
			}
		}
		data[i] = (1 - correlation)*(superRand(allowNegative) + bias) + correlation*correlValue;
		//update the current coordinates
		int j = dimCount;
		do {
			j--;
			curCoords[j]++;
			if(curCoords[j] >= dimSizes[j]) curCoords[j] = 0;
		} while(!curCoords[j] && j);
	}
	free(curCoords);
	return kNoErr;
}

/**
* Add a pure sine wave to the data array, if clearData is specified, the effect is as if the data had been zeroed before the call.
*/
ErrorCode addSine(size_t dimCount, size_t* dimSizes, double* data, double* frequencyVector, double phase, bool clearData) {
	allocate(size_t, curCoords, dimCount);
	if(!curCoords) return kErrNoMem;
	size_t i;
	size_t variableSize = 1;
	for(i = 0; i < dimCount; i++) {
		curCoords[i] = 0;
		variableSize *= dimSizes[i];
	}

	//update the data
	for(i = 0; i < variableSize; i++) {
		size_t j;
		double argument = phase;
		for(j = 0; j < dimCount; j++) {
			argument += frequencyVector[j]*curCoords[j];
		}
		if(clearData) data[i] = 0;
		data[i] += sin(argument);
		//update the current coordinates
		j = dimCount;
		do {
			j--;
			curCoords[j]++;
			if(curCoords[j] >= dimSizes[j]) curCoords[j] = 0;
		} while(!curCoords[j] && j);
	}
	free(curCoords);
	return kNoErr;
}

//Increases the sampling frequency on all dimensions by a power of 2. Requires given values in all corners, i. e. the output dimension size must be (2*inputDimensionSize - 1) for all dimensions, otherwise kErrParam is returned.
ErrorCode multilinearInterpolation(size_t dimCount, size_t* iDimSizes, size_t* oDimSizes, double* iData, double* oData, double noise) {
	//Check the precondition and calc the data sizes.
	size_t i, j;
	size_t iValueCount = 1, oValueCount = 1;
	for(i = 0; i < dimCount; i++) {
		if(oDimSizes[i] != 2*iDimSizes[i] - 1) return kErrParam;
		oValueCount *= oDimSizes[i];
		iValueCount *= iDimSizes[i];
	}

	//Allocate some mem.
	allocate(size_t, inputCoords, dimCount);
	if(!inputCoords) return kErrNoMem;
	allocate(size_t, curCoords, dimCount);
	if(!curCoords) return kErrNoMem;
	allocate(size_t, curOffset, dimCount);
	if(!curOffset) return kErrNoMem;
	for(i = 0; i < dimCount; i++) inputCoords[i] = curCoords[i] = 0;

	for(i = 0; i < oValueCount; i++) {
		//Get the sum of the surrounding given pixels.
		double sum = 0;
		long pixelCount = 0, curDim = dimCount - 1;
		for(j = 0; j < dimCount; j++) {
			curOffset[j] = 0;
			inputCoords[j] = curCoords[j] >> 1;
		}
		do {
			long inputIndex = 0;
			for(j = 0; j < dimCount; j++) {
				inputIndex *= iDimSizes[j];
				inputIndex += inputCoords[j] + curOffset[j];
			}
			sum += iData[inputIndex];
			pixelCount++;

			curDim = dimCount-1;
			while(curDim >= 0 && (curOffset[curDim] || !(curCoords[curDim] & 1))) {
				curOffset[curDim--] = 0;
			}
			if(curDim >= 0) curOffset[curDim] = 1;
		} while(curDim >= 0);

		//Write the output pixel.
		oData[i] = superRand(1)*noise + sum/pixelCount;

		//Update the current coordinates.
		int j = dimCount;
		do {
			j--;
			curCoords[j]++;
			if(curCoords[j] >= oDimSizes[j]) curCoords[j] = 0;
		} while(!curCoords[j] && j);
	}
	//Tidy up.
	free(inputCoords);
	free(curCoords);
	free(curOffset);
	return kNoErr;
}

ErrorCode integrate(long dimCount, size_t* dimSizes, double* data) {
	long i, j, valueCount = 1;
	size_t offset = 1;
	for(i = 0; i < dimCount; i++) valueCount *= dimSizes[i];
	allocate(size_t, curCoords, dimCount);
	if(!curCoords) return kErrNoMem;

	for(i = dimCount-1; i >= 0; i--) {
		for(j = 0; j < dimCount; j++) curCoords[j] = 0;
		for(j = 0; j < valueCount; j++) {
			if(curCoords[i]) {
				data[j] += data[j-offset];
			}

			//Update the current coordinates.
			long k = dimCount;
			do {
				k--;
				curCoords[k]++;
				if(curCoords[k] >= dimSizes[k]) curCoords[k] = 0;
			} while(!curCoords[k] && k);
		}
		offset *= dimSizes[i];
	}
	return kNoErr;
}

void addBias(long count, double* buffer, double bias) {
	long i;
	for(i = 0; i < count; i++) buffer[i] += bias;
}

void doubleArrayToFloatArray(long count, double* input, float* output) {
	long i;
	for(i = 0; i < count; i++) output[i] = (float)(input[i]);
}

ErrorCode doubleArrayToIntArray(long count, double* input, int32_t* output, int precisionBits) {
	if(precisionBits < 1 || precisionBits > 32) return kErrParam;
	double min = INFINITY, max = -INFINITY;
	long i;
	for(i = 0; i < count; i++) {
		if(min > input[i]) min = input[i];
		if(max < input[i]) max = input[i];
	}
	double quantum = max - min;
	for(; precisionBits > 0; precisionBits--) quantum /= 2;
	for(i = 0; i < count; i++) output[i] = (int32_t)round(input[i]/quantum);
	return kNoErr;
}

/**
* mode = 0 (int allowNegative, double bias, double correlation, int correlDim):
* Generate random data. AllowNegative controlls, whether negative random numbers are generated and the bias is added. If correlDim is negative, correlation is done with the average of all previous neighbours. Otherwise, correlation is restricted to the given dimension.
*
* mode = 1 (int allowNegative, double bias, double correlation, int correlDim):
* Generate random data as in mode = 0, but only at half the resolution in all directions. Interpolate to generate the output data. Output dimension size must be odd.
*
* mode = 2 (int allowNegative, double bias, double correlation, int correlDim):
* Same as mode = 1, but does two interpolation steps. Thus, the output dimension size modulo 4 must be 1.
*
*
*
* mode = 10 (double bias):
* Fractal mode. Start with 2x2x...x2 hypercube, the repeatedly interpolate adding noise until the output size is reached. Output dimension size must be 2^n+1.
*
* mode = 11 (double bias, double discountFactor):
* Same as mode = 10 but reduces the random amount between interpolations by the given factor (should be between 0 and 1)
*
* mode = 12 (double bias):
* Same as mode = 10 but reduces the random amount linearily towards zero (the last step does still get a minimal amount of randomization).
*
*
*
* mode = 21 (int allowNegative, double bias):
* Apply an integration step after the random field generation, the bias is added after the integration.
*
* mode = 22 (int allowNegative, double bias):
* Same as mode = 21 but two integration steps.
*
* mode = 23 (int allowNegative, double bias):
* Same as mode = 21 but three integration steps.
*
*
*
* mode = 30 (int allowNegative, double bias, int precision):
* Uncorrelated data (mode = 0) converted to ints using the formula intVal = floatVal*2^precision/(max-min), that is, precision bits are used to encode the position of the number in the entire data range. For data with a strong bias, the data might not be reconstructable due to overflow.
*
* mode = 31 (int allowNegative, double bias, int precision):
* Integrated data (mode = 21) converted to ints, the bias is added after the integration.
*
* mode = 32 (int allowNegative, double bias, int precision):
* Integrated data (mode = 22) converted to ints, the bias is added after the integration.
*
* mode = 33 (int allowNegative, double bias, int precision):
* Integrated data (mode = 23) converted to ints, the bias is added after the integration.
*
*
*
* mode = 40 ():
* Pure sine waves in superposition, one for each dimension. Different frequencies, the dataset size is a period of all sines (one sharp peak in the fourier transform for each sine).
*
* mode = 41 (int useDifferentFrequencies, int sineCount):
* Superposition sineCount waves with random directions and random phase. If useDifferentFrequencies is specified, the angular frequencies are selected randomly from the interval [0,1[, otherwise all waves use an angular frequency of 1.
*
int main_alt(int argc, char** argv) {
	int result;
	int i, j;
	int outputFile;

	srand(kRandSeed);
	//open the file
	if(argc < 3) return kErrParam;
	char* outputPath = argv[1];
	int mode = strtol(argv[2], NULL, 0);
	if(nc_create(outputPath, NC_CLOBBER | NC_64BIT_OFFSET, &outputFile)) return kErrCantOpenForWrite;

	//write the dimension descriptions
	for(i = 0; i < kDimensionCount; i++) {
		char *name;
		int outputDim;
		if(asprintf(&name, "dimension %i", i+1) <= 0) return kErrNoMem;
		if(i) {
			if(nc_def_dim(outputFile, name, kDimensionSize, &outputDim)) return kErrInternal;
		} else {
			if(nc_def_dim(outputFile, name, NC_UNLIMITED, &outputDim)) return kErrInternal;
		}
		free(name);
	}
	allocate(int, dimIds, kDimensionCount);
	if(!dimIds) return kErrNoMem;
	for(i = 0; i < kDimensionCount; i++) dimIds[i] = i;

	//write the variable description
	int outputVar;
	if(nc_def_var(outputFile, "variable", NC_FLOAT, kDimensionCount, dimIds, &outputVar)) return kErrInternal;
	const char* attributeText = "air_temperature";
	if(nc_put_att_text(outputFile, outputVar, "standard_name", strlen(attributeText)+1, attributeText)) return kErrInternal;
	attributeText = "K";
	if(nc_put_att_text(outputFile, outputVar, "units", strlen(attributeText)+1, attributeText)) return kErrInternal;

	free(dimIds);
	if(nc_enddef(outputFile)) return kErrInternal;

	//get some mem
	size_t variableSize = 1;
	for(i = 0; i < kDimensionCount; i++) variableSize *= kDimensionSize;
	allocate(double, buffer, variableSize);
	if(!buffer) return kErrNoMem;
	allocate(double, buffer2, variableSize);
	if(!buffer2) return kErrNoMem;
	allocate(void, oBuffer, 4*variableSize);
	if(!oBuffer) return kErrNoMem;
	long oBufferIsFloat = 1;	//Set to false if the oBuffer contains ints.

	allocate(size_t, startCoords, kDimensionCount);
	if(!startCoords) return kErrNoMem;
	allocate(size_t, dimSizes, kDimensionCount);
	if(!dimSizes) return kErrNoMem;
	allocate(size_t, dimSizes2, kDimensionCount);
	if(!dimSizes2) return kErrNoMem;
	for(i = 0; i < kDimensionCount; i++) {
		startCoords[i] = 0;
		dimSizes[i] = kDimensionSize;
	}

	//make up some data
	switch(mode) {
		case 0: {
			if(argc != 7) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0), correlDim = strtol(argv[6], NULL, 0);
			double bias = strtod(argv[4], NULL), correlation = strtod(argv[5], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, bias, correlation, correlDim))) return kErrInternal;
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 1: {
			if(argc != 7) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0), correlDim = strtol(argv[6], NULL, 0);
			double bias = strtod(argv[4], NULL), correlation = strtod(argv[5], NULL);

			for(i = 0; i < kDimensionCount; i++) dimSizes2[i] = (kDimensionSize + 1) >> 1;
			if((result = makeUpData(kDimensionCount, dimSizes2, buffer2, allowNegative, bias, correlation, correlDim))) return result;
            if((result = multilinearInterpolation(kDimensionCount, dimSizes2, dimSizes, buffer2, buffer, 0.0))) return result;
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 2: {
			if(argc != 7) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0), correlDim = strtol(argv[6], NULL, 0);
			double bias = strtod(argv[4], NULL), correlation = strtod(argv[5], NULL);

			for(i = 0; i < kDimensionCount; i++) dimSizes[i] = (kDimensionSize >> 2) + 1;
			for(i = 0; i < kDimensionCount; i++) dimSizes2[i] = (kDimensionSize >> 1) + 1;
			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, bias, correlation, correlDim))) return result;
			if((result = multilinearInterpolation(kDimensionCount, dimSizes, dimSizes2, buffer, buffer2, 0.0))) return result;
			for(i = 0; i < kDimensionCount; i++) dimSizes[i] = kDimensionSize;
			if((result = multilinearInterpolation(kDimensionCount, dimSizes2, dimSizes, buffer2, buffer, 0.0))) return result;
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 10: {
			if(!(kDimensionSize & 1)) return kErrParam;
			if(argc != 4) return kErrParam;
			double bias = strtod(argv[3], NULL);

			long interpolationSteps = 0;
			for(; (1 << interpolationSteps) + 1 < kDimensionSize; interpolationSteps++) ;
			if((1 << interpolationSteps) + 1 != kDimensionSize) return kErrParam;
			for(i = 0; i < kDimensionCount; i++) dimSizes[i] = 2;
			if((result = makeUpData(kDimensionCount, dimSizes, buffer, 1, bias, 0.0, 0))) return result;
			for(; interpolationSteps > 0; interpolationSteps--) {
				for(i = 0; i < kDimensionCount; i++) dimSizes2[i] = (dimSizes[i] << 1) - 1;
				if((result = multilinearInterpolation(kDimensionCount, dimSizes, dimSizes2, buffer, buffer2, 1.0))) return result;
				size_t* temp1 = dimSizes;
				double* temp2 = buffer;
				dimSizes = dimSizes2;
				buffer = buffer2;
				dimSizes2 = temp1;
				buffer2 = temp2;
			}
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 11: {
			if(!(kDimensionSize & 1)) return kErrParam;
			if(argc != 5) return kErrParam;
			double bias = strtod(argv[3], NULL), discountFactor = strtod(argv[4], NULL);

			long interpolationSteps = 0;
			double curRandomFactor = 1.0;
			for(; (1 << interpolationSteps) + 1 < kDimensionSize; interpolationSteps++) ;
			if((1 << interpolationSteps) + 1 != kDimensionSize) return kErrParam;
			for(i = 0; i < kDimensionCount; i++) dimSizes[i] = 2;
			if((result = makeUpData(kDimensionCount, dimSizes, buffer, 1, bias, 0.0, 0))) return result;
			for(; interpolationSteps > 0; interpolationSteps--) {
				for(i = 0; i < kDimensionCount; i++) dimSizes2[i] = (dimSizes[i] << 1) - 1;
				if((result = multilinearInterpolation(kDimensionCount, dimSizes, dimSizes2, buffer, buffer2, curRandomFactor))) return result;
				size_t* temp1 = dimSizes;
				double* temp2 = buffer;
				dimSizes = dimSizes2;
				buffer = buffer2;
				dimSizes2 = temp1;
				buffer2 = temp2;
				curRandomFactor *= discountFactor;
			}
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 12: {
			if(!(kDimensionSize & 1)) return kErrParam;
			if(argc != 4) return kErrParam;
			double bias = strtod(argv[3], NULL);

			long interpolationSteps = 0;
			double curRandomFactor = 1.0;
			for(; (1 << interpolationSteps) + 1 < kDimensionSize; interpolationSteps++) ;
			if((1 << interpolationSteps) + 1 != kDimensionSize) return kErrParam;
			double randomReduction = curRandomFactor/interpolationSteps;
			for(i = 0; i < kDimensionCount; i++) dimSizes[i] = 2;
			if((result = makeUpData(kDimensionCount, dimSizes, buffer, 1, bias, 0.0, 0))) return result;
			for(; interpolationSteps > 0; interpolationSteps--) {
				for(i = 0; i < kDimensionCount; i++) dimSizes2[i] = (dimSizes[i] << 1) - 1;
				if((result = multilinearInterpolation(kDimensionCount, dimSizes, dimSizes2, buffer, buffer2, curRandomFactor))) return result;
				size_t* temp1 = dimSizes;
				double* temp2 = buffer;
				dimSizes = dimSizes2;
				buffer = buffer2;
				dimSizes2 = temp1;
				buffer2 = temp2;
				curRandomFactor -= randomReduction;
			}
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 21: {
			if(argc != 5) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0);
			double bias = strtod(argv[4], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, 0.0, 0.0, 0))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			addBias(variableSize, buffer, bias);
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 22: {
			if(argc != 5) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0);
			double bias = strtod(argv[4], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, 0.0, 0.0, 0))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			addBias(variableSize, buffer, bias);
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 23: {
			if(argc != 5) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0);
			double bias = strtod(argv[4], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, 0.0, 0.0, 0))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			addBias(variableSize, buffer, bias);
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 30: {
			if(argc != 6) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0), precision = strtol(argv[5], NULL, 0);
			double bias = strtod(argv[4], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, 0, 0.0, 0))) return kErrInternal;
			addBias(variableSize, buffer, bias);
			if((result = doubleArrayToIntArray(variableSize, buffer, oBuffer, precision))) return result;
			oBufferIsFloat = 0;
		} break; case 31: {
			if(argc != 6) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0), precision = strtol(argv[5], NULL, 0);
			double bias = strtod(argv[4], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, 0, 0.0, 0))) return kErrInternal;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			addBias(variableSize, buffer, bias);
			if((result = doubleArrayToIntArray(variableSize, buffer, oBuffer, precision))) return result;
			oBufferIsFloat = 0;
		} break; case 32: {
			if(argc != 6) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0), precision = strtol(argv[5], NULL, 0);
			double bias = strtod(argv[4], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, 0, 0.0, 0))) return kErrInternal;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			addBias(variableSize, buffer, bias);
			if((result = doubleArrayToIntArray(variableSize, buffer, oBuffer, precision))) return result;
			oBufferIsFloat = 0;
		} break; case 33: {
			if(argc != 6) return kErrParam;
			int allowNegative = strtol(argv[3], NULL, 0), precision = strtol(argv[5], NULL, 0);
			double bias = strtod(argv[4], NULL);

			if((result = makeUpData(kDimensionCount, dimSizes, buffer, allowNegative, 0, 0.0, 0))) return kErrInternal;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			if((result = integrate(kDimensionCount, dimSizes, buffer))) return result;
			addBias(variableSize, buffer, bias);
			if((result = doubleArrayToIntArray(variableSize, buffer, oBuffer, precision))) return result;
			oBufferIsFloat = 0;
		} break; case 40: {
			if(argc != 3) return kErrParam;
			double frequencyVector[kDimensionCount];

			for(i = 0; i < kDimensionCount; i++) {
				for(j = 0; j < kDimensionCount; j++) frequencyVector[j] = 0;
				frequencyVector[i] = kDimensionSize/10*(i+1)/(double)kDimensionSize*2*M_PI;
				if((result = addSine(kDimensionCount, dimSizes, buffer, frequencyVector, 0, !i))) return result;
			}
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		} break; case 41: {
			if(argc != 5) return kErrParam;
			int useDifferentFrequencies = strtol(argv[3], NULL, 0), sineCount = strtol(argv[4], NULL, 0);
			double frequencyVector[kDimensionCount];

			for(i = 0; i < sineCount; i++) {
				double squaredLength, factor;
				do {
					squaredLength = 0;
					for(j = 0; j < kDimensionCount; j++) {
						frequencyVector[j] = superRand(1);
						squaredLength += frequencyVector[j]*frequencyVector[j];
					}
				} while(squaredLength >= 1 || squaredLength < 1e-100);	//this guarantees equal distribution in terms of direction
				factor = ((useDifferentFrequencies) ? superRand(1) : 1)/sqrt(squaredLength);
				for(j = 0; j < kDimensionCount; j++) frequencyVector[j] *= factor;
				if((result = addSine(kDimensionCount, dimSizes, buffer, frequencyVector, superRand(0)*2*M_PI, !i))) return result;
			}
			doubleArrayToFloatArray(variableSize, buffer, oBuffer);
		}
	}

	//write the fake data
	if(nc_put_vara_float(outputFile, outputVar, startCoords, dimSizes, oBuffer)) return kErrInternal;

	//visualize the first record of fake data
	char* povFileName;
	if(asprintf(&povFileName, "%s.pov", outputPath) <= 0) return kErrNoMem;
	FILE* povFile = fopen(povFileName, "w");
	if(!povFile) return kErrCantOpenForWrite;
	fprintf(povFile, "#include \"coordinatesystemtemplate.inc\"\n");
	fprintf(povFile, "\n");
	fprintf(povFile, "#declare flipYAxis = false;\n");
	fprintf(povFile, "#declare makeTangentPlane = false;\n");
	fprintf(povFile, "\n");
	fprintf(povFile, "#declare data = array[%i][%i] {", kDimensionSize, kDimensionSize);
	for(i = 0; i < kDimensionSize; i++) {
		if(i) fprintf(povFile, ",");
		fprintf(povFile, "\n\t{");
		for(j = 0; j < kDimensionSize; j++) {
			if(j) fprintf(povFile, ",");
			if(oBufferIsFloat) {
				fprintf(povFile, " %g", ((float*)oBuffer)[i*kDimensionSize + j]);
			} else {
				fprintf(povFile, " %i", ((int*)oBuffer)[i*kDimensionSize + j]);
			}
		}
		fprintf(povFile, " }");
	}
	fprintf(povFile, "}");
	fprintf(povFile, "\n");
	fprintf(povFile, "makeGraph(data, <%i, %i>, <%i, %i>)\n", kDimensionSize/2, kDimensionSize/2, kDimensionSize, kDimensionSize);
	if((result = fclose(povFile))) return result;

	//tidy up
	free(dimSizes);
	free(startCoords);
	free(buffer);
	free(buffer2);
	free(oBuffer);
	if(nc_close(outputFile)) return kErrCantCloseFile;
	return 0;
}*/

static int write_to_csv(const double bias, const double discountFactor, const scil_user_hints_t hints, const size_t uncompressed_size, const size_t compressed_size, const double compression_ratio, const double seconds){

	char path[128];
	sprintf(path, "performance_data_%.2f_%.2f.csv", bias, discountFactor);

	FILE* csv = fopen(path, "a");
	fprintf(csv, "%s,%.15lf,%.15lf,%d,%d,%lu,%lu,%lf,%lf\n",
		hints.force_compression_methods,
		hints.absolute_tolerance,
		hints.relative_tolerance_percent,
		hints.significant_digits,
		hints.significant_bits,
		uncompressed_size,
		compressed_size,
		compression_ratio,
		seconds);
	fclose(csv);

	return 0;
}

int test_performance(double bias, double discountFactor){

	int result = 0;

	//get some mem
	size_t variableSize = 1;
	for(uint8_t i = 0; i < kDimensionCount; i++) variableSize *= kDimensionSize;

	printf("Initializing data generation.\n");

	allocate(double, buffer, variableSize);
	if(!buffer) return kErrNoMem;
	allocate(double, buffer2, variableSize);
	if(!buffer2) return kErrNoMem;
	allocate(char, oBuffer, 4*variableSize);
	if(!oBuffer) return kErrNoMem;

	// long oBufferIsFloat = 1;	//Set to false if the oBuffer contains ints.

	allocate(size_t, startCoords, kDimensionCount);
	if(!startCoords) return kErrNoMem;
	allocate(size_t, dimSizes, kDimensionCount);
	if(!dimSizes) return kErrNoMem;
	allocate(size_t, dimSizes2, kDimensionCount);
	if(!dimSizes2) return kErrNoMem;

	for(uint8_t i = 0; i < kDimensionCount; i++) {
		startCoords[i] = 0;
		dimSizes[i] = kDimensionSize;
	}

	printf("Done.\nGenerating data.\n");

	// Mode 11 -----------------------------------------
	if(!(kDimensionSize & 1)) return kErrParam;

	long interpolationSteps = 0;
	double curRandomFactor = 1.0;
	for(; (1 << interpolationSteps) + 1 < kDimensionSize; interpolationSteps++) ;
	if((1 << interpolationSteps) + 1 != kDimensionSize) return kErrParam;
	for(uint8_t i = 0; i < kDimensionCount; i++) dimSizes[i] = 2;
	if((result = makeUpData(kDimensionCount, dimSizes, buffer, 1, bias, 0.0, 0))) return result;
	for(; interpolationSteps > 0; interpolationSteps--) {
		for(uint8_t i = 0; i < kDimensionCount; i++) dimSizes2[i] = (dimSizes[i] << 1) - 1;
		if((result = multilinearInterpolation(kDimensionCount, dimSizes, dimSizes2, buffer, buffer2, curRandomFactor))) return result;
		size_t* temp1 = dimSizes;
		double* temp2 = buffer;
		dimSizes = dimSizes2;
		buffer = buffer2;
		dimSizes2 = temp1;
		buffer2 = temp2;
		curRandomFactor *= discountFactor;
	}
	doubleArrayToFloatArray(variableSize, buffer, (float*)oBuffer);
	// -----------------------------------------------------

	printf("Done.\nTesting algorithm performances.\n");

	const size_t c_size = (variableSize * sizeof(double)+SCIL_BLOCK_HEADER_MAX_SIZE);

	allocate(byte, buffer_out, c_size);
	if(!buffer_out) return kErrNoMem;

	scil_dims_t dims;
	scilPr_initialize_dims_1d(& dims, variableSize);

  scil_context_t* ctx;
  scil_user_hints_t hints;

  scilPr_initialize_user_hints(&hints);

  hints.force_compression_methods = "0";
	hints.absolute_tolerance = 0.5;
	hints.significant_bits = 1;

	char pipeline[100];

	hints.force_compression_methods = pipeline;
	for(int i=0; i < scilU_get_available_compressor_count(); i++ ){
		sprintf(pipeline, "%d", i);

		double abs_tol = 0.5;
		for(uint32_t r = 1; r < 16; ++r){

			//if((hints.force_compression_method == 0 || hints.force_compression_method == 2 || hints.force_compression_method == 4) && r > 1) break;

			hints.absolute_tolerance = abs_tol;
			hints.significant_bits = r;

			scilPr_create_context(&ctx, SCIL_TYPE_DOUBLE, &hints);
			size_t out_c_size = c_size;

			double seconds = 0;

			uint8_t loops = 10;
			for(uint8_t i = 0; i < loops; ++i){

				clock_t start, end;
				start = clock();
				scil_compress(buffer_out, c_size, oBuffer, & dims, &out_c_size, ctx);
				end = clock();

				seconds += (double)(end - start);
			}
			seconds /= (loops * CLOCKS_PER_SEC);

			size_t u_size = variableSize * sizeof(double);
			double c_fac = (double)(u_size) / out_c_size;

			printf("Compressing with %s:\n", hints.force_compression_methods);
			printf("\tAbsolute tolerance:\t%.15lf\n", hints.absolute_tolerance);
			printf("\tRelative tolerance:\t%lf%%\n", hints.relative_tolerance_percent);
			printf("\tSignificant digits:\t%d\n", hints.significant_digits);
			printf("\tSignificant bits:\t%d\n", hints.significant_bits);
	        printf("\tUncompressed size:\t%lu\n", u_size);
	        printf("\tCompressed size:\t%lu\n", out_c_size);
	        printf("\tCompression factor:\t%lf\n", c_fac);
	        printf("\tCompression time:\t%lf\n\n", seconds);

			write_to_csv(bias, discountFactor, hints, u_size, out_c_size, c_fac, seconds);

			abs_tol *= 0.5;
		}
    }

	printf("Done.\n");

	free(buffer);
	free(buffer2);
	free(oBuffer);
	free(startCoords);
	free(dimSizes);
	free(dimSizes2);
	free(buffer_out);

	return 0;
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
int main(int argc, char** argv){
	test_performance(0.0f, strtod(argv[1], NULL));
  return 0;
}
