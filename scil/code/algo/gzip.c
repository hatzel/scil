#include <algo/gzip.h>

#include <zlib.h>

int scil_gzip_compress(const scil_context* ctx, byte* restrict dest, size_t* restrict dest_size, const double*restrict source, const size_t source_count){
  return compress( (Bytef*)dest, dest_size, (Bytef*)source, (uLong)(source_count * sizeof(double)) ) == Z_OK ;
}

int scil_gzip_decompress(const scil_context* ctx, double*restrict dest, size_t*restrict dest_count, const byte*restrict source, const size_t source_size){
    uLongf dest_size = *dest_count * sizeof(double);
    int ret = uncompress( (Bytef*)dest, & dest_size, (Bytef*)source, (uLong)source_size);

    if(ret != Z_OK){
        fprintf(stderr, "Error in gzip decompression. (Buf error: %d mem error: %d data_error: %d size: %lld)\n",
        ret == Z_BUF_ERROR , ret == Z_MEM_ERROR, ret == Z_DATA_ERROR, (long long) dest_size);
    }

    *dest_count = dest_size / sizeof(double);
    return ret;
}

scil_compression_algorithm algo_gzip = {
    scil_gzip_compress,
    scil_gzip_decompress,
    "gzip",
    2
};