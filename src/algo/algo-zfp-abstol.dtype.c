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

#include <algo/algo-zfp-abstol.h>

#include <string.h>

#include <scil-util.h>

#include <zfp.h>

//Supported datatypes: float double
// Repeat for each data type
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wunused-parameter"
int scil_zfp_abstol_compress_<DATATYPE>(const scil_context_t* ctx,
                        byte * restrict dest,
                        size_t* restrict dest_size,
                        <DATATYPE>*restrict source,
                        const scil_dims_t* dims)
{
    int ret = 0;

    *dest_size = 0;

    *((double*)dest) = (ctx->hints.absolute_tolerance == SCIL_ACCURACY_DBL_FINEST) ? 0 : ctx->hints.absolute_tolerance;
    dest += 8;
    *dest_size += 8;

    // Compress
    zfp_field* field = NULL;

    switch(dims->dims){
        case 1: field = zfp_field_1d(source, zfp_type_<DATATYPE>, dims->length[0]); break;
        case 2: field = zfp_field_2d(source, zfp_type_<DATATYPE>, dims->length[0], dims->length[1]); break;
        case 3: field = zfp_field_3d(source, zfp_type_<DATATYPE>, dims->length[0], dims->length[1], dims->length[2]); break;
        default: field = zfp_field_1d(source, zfp_type_<DATATYPE>, scilPr_get_dims_count(dims));
    }

    zfp_stream* zfp = zfp_stream_open(NULL);

    /*  zfp_stream_set_rate(zfp, rate, type, 3, 0); */
    /*  zfp_stream_set_precision(zfp, precision, type); */
    zfp_stream_set_accuracy(zfp, ctx->hints.absolute_tolerance, zfp_type_<DATATYPE>);

    size_t bufsize = zfp_stream_maximum_size(zfp, field);
    bitstream* stream = stream_open(dest, bufsize);
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    *dest_size += zfp_compress(zfp, field);
    if(*dest_size == 0){
        fprintf(stderr, "ZPF compression failed\n");
        ret = 1;
    }

    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return ret;
}

#pragma GCC diagnostic ignored "-Wunused-parameter"
int scil_zfp_abstol_decompress_<DATATYPE>( <DATATYPE>*restrict data_out,
                            scil_dims_t* dims,
                            byte*restrict compressed_buf_in,
                            const size_t in_size)
{
    int ret = 0;

    double tolerance = *((double*)compressed_buf_in);
    compressed_buf_in += 8;

    // Decompress
    zfp_field* field = NULL;

    switch(dims->dims){
        case 1: field = zfp_field_1d(data_out, zfp_type_<DATATYPE>, dims->length[0]); break;
        case 2: field = zfp_field_2d(data_out, zfp_type_<DATATYPE>, dims->length[0], dims->length[1]); break;
        case 3: field = zfp_field_3d(data_out, zfp_type_<DATATYPE>, dims->length[0], dims->length[1], dims->length[2]); break;
        default: field = zfp_field_1d(data_out, zfp_type_<DATATYPE>, scilPr_get_dims_count(dims));
    }

    zfp_stream* zfp = zfp_stream_open(NULL);

    /*  zfp_stream_set_rate(zfp, rate, type, 3, 0); */
    /*  zfp_stream_set_precision(zfp, precision, type); */
    zfp_stream_set_accuracy(zfp, tolerance, zfp_type_<DATATYPE>);

    size_t bufsize = zfp_stream_maximum_size(zfp, field);
    bitstream* stream = stream_open(compressed_buf_in, bufsize);
    zfp_stream_set_bit_stream(zfp, stream);
    zfp_stream_rewind(zfp);

    if(!zfp_decompress(zfp, field)){
        fprintf(stderr, "ZPF compression failed\n");
        ret = 1;
    }

    zfp_field_free(field);
    zfp_stream_close(zfp);
    stream_close(stream);

    return ret;
}

// End repeat

scilI_algorithm_t algo_zfp_abstol = {
    .c.DNtype = {
        CREATE_INITIALIZER(scil_zfp_abstol)
    },
    "zfp-abstol",
    5,
    SCIL_COMPRESSOR_TYPE_DATATYPES,
    1
};
