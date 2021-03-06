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

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netcdf.h>

#include <scil-util.h>
#include <plugins/file-netcdf.h>

static char * netcdf_varname = "";

static option_help options [] = {
  {0, "netcdf-varname", "Name of variable to compress",  OPTION_OPTIONAL_ARGUMENT, 's', & netcdf_varname},
  LAST_OPTION
};

static option_help * get_options(){
  return options;
}

/* reverse string */
void reverse(char s[])
{
    int i, j;
    char c;

    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* integer to string*/
void itoa(int n, char s[])
 {
     int i, sign;

     if ((sign = n) < 0)
         n = -n;
     i = 0;
     do {
         s[i++] = n % 10 + '0';
     } while ((n /= 10) > 0);
     if (sign < 0)
         s[i++] = '-';
     s[i] = '\0';
     reverse(s);
 }

static int readData(const char * name, byte ** out_buf, SCIL_Datatype_t * out_datatype, scil_dims_t * out_dims, size_t * read_size){
  printf("READDATA\n");
  int ncid;
  int ndims_in, nvars_in, ngatts_in, unlimdimid_in;
  int rh_id;
  size_t lengthp[NC_MAX_VAR_DIMS];
  int formatp, rh_ndims, rh_dimids[NC_MAX_VAR_DIMS];
  nc_type rh_type;

  /* Error handling. */
  int retval;

  byte *input_data = NULL;

  if (netcdf_varname == NULL)
  {
    printf("ERROR netcdf_varname is empty\n");
    return 1;
  }
  /* Open the file. */
  if ((retval = nc_open(name, NC_NOWRITE, &ncid)))
  {
    NC_ISSYSERR(retval);
    return 1;
  }
  nc_inq_format(ncid, &formatp);
  printf("%s file opened for read\n", (formatp == NC_FORMAT_CLASSIC)?"NC_FORMAT_CLASSIC":(formatp == NC_FORMAT_64BIT_OFFSET)?"NC_FORMAT_64BIT_OFFSET":(formatp == NC_FORMAT_NETCDF4)?"NC_FORMAT_NETCDF4":(formatp == NC_FORMAT_CDF5)?"NC_FORMAT_CDF5":"another");

  if ((retval = nc_inq(ncid, &ndims_in, &nvars_in, &ngatts_in, &unlimdimid_in)))
  {
    NC_ISSYSERR(retval);
    return 1;
  }

  printf("\n\n====OUTPUT====\nnumber of dimensions: %i\nnumber of variables: %i\nnumber of global attributes: %i\nunlimited dimentions: %i\n\n", ndims_in, nvars_in, ngatts_in, unlimdimid_in);

  /*Get variable*/
  if ((retval = nc_inq_varid (ncid, netcdf_varname, &rh_id)))
  {
    printf("ERROR no variable with this name");
    NC_ISSYSERR(retval);
  }

    if ((retval = nc_inq_var (ncid, rh_id, NULL, &rh_type, &rh_ndims, rh_dimids, NULL)))
      NC_ISSYSERR(retval);
    else
    {
      switch(rh_type){
        case(NC_DOUBLE):
          *out_datatype = SCIL_TYPE_DOUBLE;
          break;
        case(NC_FLOAT):
          *out_datatype = SCIL_TYPE_FLOAT;
          break;
        case(NC_BYTE):
          *out_datatype = SCIL_TYPE_INT8;
          break;
        case(NC_SHORT):
          *out_datatype = SCIL_TYPE_INT16;
          break;
        case(NC_INT):
          *out_datatype = SCIL_TYPE_INT32;
          break;
        case(NC_INT64):
          *out_datatype = SCIL_TYPE_INT64;
          break;
        default:
          printf("ERROR: not supported datatype in readData\n");
          return 1;
        }
      for (int j = 0; j < rh_ndims; j++)
        if ((retval = nc_inq_dim(ncid, rh_dimids[j], NULL, &lengthp[j])))
          NC_ISSYSERR(retval);

      switch(rh_ndims){
        case(1):
        scilPr_initialize_dims_1d(out_dims, lengthp[0]);
        break;
        case(2):
        scilPr_initialize_dims_2d(out_dims, lengthp[0], lengthp[1]);
        break;
        case(3):
        scilPr_initialize_dims_3d(out_dims, lengthp[0], lengthp[1], lengthp[2]);
        break;
        case(4):
        scilPr_initialize_dims_4d(out_dims, lengthp[0], lengthp[1], lengthp[2], lengthp[3]);
        break;
        default:
        printf("ERROR: not supported number of dimensions\n");
        return 1;
      }

      input_data = (byte*) malloc(scilPr_get_compressed_data_size_limit(out_dims, *out_datatype));

      switch(*out_datatype){
        case(SCIL_TYPE_DOUBLE):
          nc_get_var(ncid,rh_id,(double*)input_data);
          break;
        case(SCIL_TYPE_FLOAT):
          nc_get_var(ncid,rh_id,(float*)input_data);
          break;
        case(SCIL_TYPE_INT8):
          nc_get_var(ncid,rh_id,(int8_t*)input_data);
          break;
        case(SCIL_TYPE_INT16):
          nc_get_var(ncid,rh_id,(int16_t*)input_data);
          break;
        case(SCIL_TYPE_INT32):
          nc_get_var(ncid,rh_id,(int32_t*)input_data);
          break;
        case(SCIL_TYPE_INT64):
          nc_get_var(ncid,rh_id,(int64_t*)input_data);
          break;
        default:
          printf("ERROR: not supported datatype in readData\n");
          return 1;
      }
    }

  /* Close the file. */
  if ((retval = nc_close(ncid)))
  {
    NC_ISSYSERR(retval);
    return 1;
  }

  printf("*** SUCCESS reading example file %s!\n", name);

  *read_size = scilPr_get_dims_size(out_dims, *out_datatype);
  *out_buf = input_data;
  return 0;
}

static int writeData(const char * name, const byte * buf, SCIL_Datatype_t buf_datatype, size_t elements, SCIL_Datatype_t orig_datatype, scil_dims_t dims){
  printf("WRITEDATA\n");
  int ncid;
  int dimids[NC_MAX_VAR_DIMS];
  int varid;
  int ncdatatype;

  /* Error handling. */
  int retval;

  switch(buf_datatype){
    case(SCIL_TYPE_DOUBLE):
      ncdatatype = NC_DOUBLE;
      break;
    case(SCIL_TYPE_FLOAT):
      ncdatatype = NC_FLOAT;
      break;
    case(SCIL_TYPE_INT8):
      ncdatatype = NC_BYTE;
      break;
    case(SCIL_TYPE_INT16):
      ncdatatype = NC_SHORT;
      break;
    case(SCIL_TYPE_INT32):
      ncdatatype = NC_INT;
      break;
    case(SCIL_TYPE_INT64):
      ncdatatype = NC_INT64;
      break;
    default:
      printf("ERROR: not supported datatype in writeData\n");
      return 1;
    }
  /* Write new file*/
  if ((retval = nc_create(name, NC_NETCDF4, &ncid)))
    NC_ISSYSERR(retval);

  char cbuffer[5];//99+dim


  for (int i = 0; i < dims.dims; i++)
  {
    memset(cbuffer,0,strlen(cbuffer));
    itoa(i,cbuffer);
    strcat( cbuffer, "dim");
    if ((retval = nc_def_dim(ncid, cbuffer, dims.length[i], &dimids[i])))
      NC_ISSYSERR(retval);
  }


  if ((retval = nc_def_var(ncid, netcdf_varname, ncdatatype, dims.dims, dimids, &varid)))
  {
    printf("ERROR no variable with this name");
    NC_ISSYSERR(retval);
  }

  /* End define mode. This tells netCDF we are done defining
   * metadata. */
  if ((retval = nc_enddef(ncid)))
     NC_ISSYSERR(retval);

  switch(buf_datatype){//output_datatype
       case(SCIL_TYPE_DOUBLE):
       if ((retval = nc_put_var_double(ncid, varid, (const double*) buf)))
         NC_ISSYSERR(retval);
         break;
       case(SCIL_TYPE_FLOAT):
       if ((retval = nc_put_var_float(ncid, varid, (const float*) buf)))
         NC_ISSYSERR(retval);
         break;
       case(SCIL_TYPE_INT8):
       if ((retval = nc_put_var_text(ncid, varid, (const char*) buf)))
         NC_ISSYSERR(retval);
         break;
       case(SCIL_TYPE_INT16):
       if ((retval = nc_put_var_short(ncid, varid, (const short*) buf)))
         NC_ISSYSERR(retval);
         break;
       case(SCIL_TYPE_INT32):
       if ((retval = nc_put_var_int(ncid, varid, (const int*) buf)))
         NC_ISSYSERR(retval);
         break;
       case(SCIL_TYPE_INT64):
       if ((retval = nc_put_var_longlong(ncid, varid, (const long long*) buf)))
         NC_ISSYSERR(retval);
         break;
       default:
         printf("Not supported datatype in writeData\n");
       }

  /* Close the file. */
  if ((retval = nc_close(ncid)))
  {
    NC_ISSYSERR(retval);
    return 1;
  }

  printf("*** SUCCESS writing file %s!\n", name);


  return 0;
}

scil_file_plugin_t netcdf_plugin = {
  "netcdf",
  "nc",
  get_options,
  readData,
  writeData
};
