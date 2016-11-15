// This file is part of SCIL.
//
// SCIL is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// SCIL is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with SCIL.  If not, see <http://www.gnu.org/licenses/>.

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <pthread.h>

#include <scil-compressors.h>
#include <scil-algo-chooser.h>
#include <scil-internal.h>
#include <scil-patterns.h>
#include <scil-util.h>

#define allocate(type, name, count) type* name = (type*)malloc(count * sizeof(type))

enum metrics {
    DATA_SIZE,
    ELEMENT_COUNT,
    DIMENSIONALITY,
    MINIMUM,
    MAXIMUM,
    MEAN,
    MEDIAN,
    STANDARD_DEVIATION,
    MAX_STEP_SIZE,
    ABS_ERR_TOL,
    REL_ERR_TOL
};

#define AVAILABLE_METRICS_COUNT 11
static const char *available_metrics[AVAILABLE_METRICS_COUNT] = {
    "dtype",
    "ecount",
    "dsize",
    "dim",
    "min",
    "max",
    "mean",
    "stddev",
    "maxstp",
    "abserr",
    "relerr",
    NULL
};

#define DEFAULT_DTYPE   SCIL_TYPE_DOUBLE
#define DEFAULT_ECOUNT  1024
#define DEFAULT_DIM     1
#define DEFAULT_MIN     -1024
#define DEFAULT_MAX     1024
#define DEFAULT_ABS_ERR 0.005
#define DEFAULT_REL_ERR 1

#define SAMPLE_SIZE 10000

#define FILE_NAME "machine_learning_data.csv"
static FILE *file = NULL;

typedef struct line_data {
    size_t line;
    char algo[16];
    size_t size;
    size_t count;
    uint8_t dims;
    double min;
    double max;
    double mean;
    double median;
    double stddev;
    //double stepsize0;
    double abs_tol;
    double rel_tol;
    double compthru;
    double decompthru;
    double compratio;
} line_data_t;

static line_data_t current_data = { 0, "", 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

static void write_line(){
    printf("%lu,%s,%lu,%lu,%u,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", current_data.line,
                                                                current_data.algo,
                                                                current_data.size,
                                                                current_data.count,
                                                                current_data.dims,
                                                                current_data.min,
                                                                current_data.max,
                                                                current_data.mean,
                                                                current_data.median,
                                                                current_data.stddev,
                                                                current_data.abs_tol,
                                                                current_data.rel_tol,
                                                                current_data.compthru,
                                                                current_data.decompthru,
                                                                current_data.compratio);

    fprintf(file, "%lu,%s,%lu,%lu,%u,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", current_data.line,
                                                                current_data.algo,
                                                                current_data.size,
                                                                current_data.count,
                                                                current_data.dims,
                                                                current_data.min,
                                                                current_data.max,
                                                                current_data.mean,
                                                                current_data.median,
                                                                current_data.stddev,
                                                                current_data.abs_tol,
                                                                current_data.rel_tol,
                                                                current_data.compthru,
                                                                current_data.decompthru,
                                                                current_data.compratio);

    current_data.line++;
}

//#define AVAILABLE_COMPRESSION_CHAINS_COUNT 15
//static const *available_compression_chains[AVAILABLE_COMPRESSION_CHAINS_COUNT] = {
//
//}

// #############################################################################
// # Data Characteristics Aquisition
// #############################################################################

static double get_data_minimum(const double* data, size_t count){

    double min = INFINITY;

    for (size_t i = 0; i < count; i++) {
        if (data[i] < min) { min = data[i]; }
    }

    return min;
}

static double get_data_maximum(const double* data, size_t count){

    double max = -INFINITY;

    for (size_t i = 0; i < count; i++) {
        if (data[i] > max) { max = data[i]; }
    }

    return max;
}

static double get_data_mean(const double* data, size_t count){

      double mn = 0.0;

      for (size_t i = 0; i < count; i++) {
          mn += data[i];
      }

      return mn / count;
}

static int compdblp(const void* a, const void* b){

    if (*(double*)a > *(double*)b){ return 1; }
    if (*(double*)a < *(double*)b){ return -1; }
    return 0;
}
static double get_data_median(const double* data, size_t count){

    allocate(double, tmp_buf, count);
    memcpy(tmp_buf, data, count * sizeof(double));

    qsort(tmp_buf, count, sizeof(double), compdblp);

    double median = 0.0;

    if (count % 2 == 0) { median = 0.5 * (tmp_buf[count/2] + tmp_buf[count/2 + 1]); }
    else                { median = tmp_buf[count/2]; }

    free(tmp_buf);

    return median;
}

static double get_data_std_deviation(const double* data, size_t count, double mean){

    double variance_sum = 0.0;

    for (size_t i = 0; i < count; i++) {
        double dif = data[i] - mean;
        variance_sum += dif * dif;
    }

    return sqrt(variance_sum / count);
}

static double get_data_std_deviation_alt(const double* data, size_t count){

    double sum = 0.0, squared_sum = 0.0;

    for (size_t i = 0; i < count; i++) {
        sum         += data[i];
        squared_sum += data[i] * data[i];
    }

    return sqrt((squared_sum - (sum * sum) / count) / count);
}

static int set_data_characteristics(const double *data, size_t count){

    current_data.min    = get_data_minimum(data, count);
    current_data.max    = get_data_maximum(data, count);
    current_data.mean   = get_data_mean(data, count);
    current_data.median = get_data_median(data, count);
    current_data.stddev = get_data_std_deviation(data, count, current_data.mean);

    return 0;
}

// #############################################################################
// # Utility Functions
// #############################################################################
static int in_strarr(const char* string, const char* const* strarr, size_t count){

    for (size_t i = 0; i < count; i++) {
        if (strcmp(strarr[i], string))
            continue;
        return 1;
    }
    return 0;
}

static int get_metric_bit_mask(const char *const *metric_args, size_t count){
    int result = 0;

    const char *const *current_metric = available_metrics;
    size_t i = 0;
    while(*current_metric != NULL){
        //printf("%s\n", *current_metric);
        result |= in_strarr(*current_metric, metric_args, count) << i;
        i++;
        current_metric++;
    }

    return result;
}

static double get_random_double_in_range(double minimum, double maximum){

    return minimum + (maximum - minimum) * (double)rand()/RAND_MAX;
}

static int get_random_integer_in_range(int minimum, int maximum){

    assert(maximum >= minimum);

    if (minimum == maximum) return minimum;

    return minimum + rand()%(maximum - minimum + 1);
}

// #############################################################################
// # Data Generation
// #############################################################################

static void evaluate_compression_algorithm(double *buffer, scil_dims_t *dims, char algo){

    scil_user_hints_t hints;
    scilPr_initialize_user_hints(&hints);

    hints.force_compression_methods  = strndup(&algo, 1);
    hints.absolute_tolerance         = current_data.abs_tol;
    hints.relative_tolerance_percent = current_data.rel_tol;

    switch (algo) {
        case '0': strncpy(current_data.algo, "memcpy"       , 16); break;
        case '1': strncpy(current_data.algo, "abstol"       , 16); break;
        case '2': strncpy(current_data.algo, "gzip"         , 16); break;
        case '3': strncpy(current_data.algo, "sigbits"      , 16); break;
        case '4': strncpy(current_data.algo, "fpzip"        , 16); break;
        case '5': strncpy(current_data.algo, "zfp_abstol"   , 16); break;
        case '6': strncpy(current_data.algo, "zfp_precision", 16); break;
        case '7': strncpy(current_data.algo, "lz4fast"      , 16); break;
    }

    scil_context_t* ctx;
    scilPr_create_context(&ctx, SCIL_TYPE_DOUBLE, 0, NULL, &hints);

    size_t source_size = scilPr_get_dims_size(dims, SCIL_TYPE_DOUBLE);
    size_t dest_size   = scilPr_get_compressed_data_size_limit(dims, SCIL_TYPE_DOUBLE);
    byte* dest         = (byte*)SAFE_MALLOC(dest_size);

    // Compression analysis
    clock_t start = clock();
    int ret = scil_compress(dest, dest_size, buffer, dims, &dest_size, ctx);
    clock_t end = clock();

    current_data.compthru = 1e-6 * source_size * CLOCKS_PER_SEC / (end - start); // MB/s

    current_data.compratio = (double)source_size / dest_size;

    double *decompd = (double *)SAFE_MALLOC(source_size);
    byte *temp = (byte *)SAFE_MALLOC(dest_size);

    // Decompression analysis
    start = clock();
    ret = scil_decompress(SCIL_TYPE_DOUBLE, decompd, dims, dest, dest_size, temp);
    end = clock();

    current_data.decompthru = 1e-6 * source_size * CLOCKS_PER_SEC / (end - start); // MB/s

    write_line();

    scilPr_destroy_context(ctx);
    free(dest);
    free(decompd);
    free(temp);
}

static void evaluate_compression_algorithms(double *buffer, scil_dims_t *dims){

    for (uint8_t i = 0; i < 8; i++) {
        char c = '0' + (char)i;
        evaluate_compression_algorithm(buffer, dims, c);
    }
}

static void generate_data(){

    // Pattern name
    char* name;
    switch(rand() % 5){
        case 0: name = "constant"; break;
        case 1: name = "random"; break;
        case 2: name = "steps"; break;
        case 3: name = "sin"; break;
        case 4: name = "simplexNoise"; break;
    }

    // Dimensionality
    current_data.dims = (uint8_t)get_random_integer_in_range(1, 4);
    size_t e_count = (size_t)pow(2.0, get_random_double_in_range(8.0, 22.0)); // maximum of 32 MB for double values
    size_t side = (size_t)pow(e_count, 1.0/current_data.dims);

    scil_dims_t dims;
    switch(current_data.dims){
        case 1: scilPr_initialize_dims_1d(&dims, side); break;
        case 2: scilPr_initialize_dims_2d(&dims, side, side); break;
        case 3: scilPr_initialize_dims_3d(&dims, side, side, side); break;
        case 4: scilPr_initialize_dims_4d(&dims, side, side, side, side); break;
    }

    current_data.size  = scilPr_get_dims_size(&dims, SCIL_TYPE_DOUBLE);
    current_data.count = scilPr_get_dims_count(&dims);

    allocate(double, data_buffer, current_data.count);

    // Minimum and maximum
    double min, max;

    double point_a = get_random_double_in_range(DEFAULT_MIN, DEFAULT_MAX);
    double point_b = get_random_double_in_range(DEFAULT_MIN, DEFAULT_MAX);

    if (point_b > point_a) { min = point_a; max = point_b; }
    else                   { min = point_b; max = point_a; }

    // Other Arguments
    uint8_t arg1 = get_random_integer_in_range(1, 16);
    uint8_t arg2 = get_random_integer_in_range(1, 16);

    printf("Generating buffer of %lu values with the %s pattern... ", current_data.count, name);
    fflush(stdout);

    scilPa_create_pattern_double(data_buffer, &dims, name, min, max, arg1, arg2);

    printf("Done!\n");

    // Data characteristics
    set_data_characteristics(data_buffer, current_data.count);

    // User Params for compression
    current_data.abs_tol = pow(2.0, get_random_integer_in_range(-13, -1));
    current_data.rel_tol = pow(2.0, get_random_integer_in_range(-10, 2));

    evaluate_compression_algorithms(data_buffer, &dims);

    free(data_buffer);
}

// #############################################################################
// # Main Function
// #############################################################################

int main(int argc, char** argv){

    srand((unsigned)time(NULL));

    file = fopen(FILE_NAME, "w");
    if(file == NULL){
        fprintf(stderr, "%s\n", "Error, opening file.");
        return 1;
    }

    fprintf(file, "%s\n", "Index,Algorithm,Size of buffer,Number of values in buffer,Dimensionality,Minimum value,Maximum value,Average,Median,Standard deviation,Absolute error tolerance,Relative error tolerance,Compression throughput,Decompression throughput,Compression ratio");

    for (size_t i = 0; i < SAMPLE_SIZE; i++){
        generate_data();
    }

    fclose(file);

    return 0;
}
