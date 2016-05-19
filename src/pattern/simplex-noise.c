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

#include <assert.h>

#include <simplex-noise.h>
#include <scil-util.h>
#include <open-simplex-noise.h>


static int simplex(scil_dims * dims, double * buffer, float mn, float mx, float arg){
  const int frequencyCount = (int) arg % 10;

  if( scilU_float_equal(mn, mx) || (int) arg <= 100  || frequencyCount <= 0 ){
    return SCIL_EINVAL;
  }
	struct osn_context *ctx;
  int64_t seed = 4711;
	open_simplex_noise(seed, &ctx);

  double highFrequency = ((int) arg / 10) / 10;
  int64_t max_potenz = 1<<frequencyCount;
  size_t * len = dims->length;

  switch(dims->dims){
    case (1):{
      size_t count = scil_get_data_count(dims);
      for (size_t i=0; i < count; i++){
        buffer[i] = 0;
        int64_t potenz = max_potenz;
        int64_t divisor = 1;
        for(int f = 1 ; f <= frequencyCount; f++){
          buffer[i] += potenz * open_simplex_noise2(ctx, 1.0, (double) i / count * divisor * highFrequency);
          potenz /= 2;
          divisor *= 2;
        }
      }
      break;
    }case (2):{
      for (size_t y=0; y < len[1]; y++){

        for (size_t x=0; x < len[0]; x++){
          double var = 0;
          int64_t potenz = max_potenz;
          int64_t divisor = 1;
          for(int f = 1 ; f <= frequencyCount; f++){
            var += potenz * open_simplex_noise2(ctx, (double) x / len[0] * divisor*highFrequency, (double) y / len[1] * divisor*highFrequency);
            potenz /= 2;
            divisor *= 2;
          }
          buffer[x+y*len[0]] = var;
        }

      }
      break;
    }case (3):{
      for (size_t z=0; z < len[2]; z++){
        for (size_t y=0; y < len[1]; y++){
          for (size_t x=0; x < len[0]; x++){
            double var = 0;
            int64_t potenz = max_potenz;
            int64_t divisor = 1;
            for(int f = 1 ; f <= frequencyCount; f++){
              var += potenz * open_simplex_noise3(ctx, (double) x / len[0]*divisor*highFrequency, (double) y / len[1]*divisor*highFrequency, (double) z / len[2]*divisor*highFrequency);
              potenz /= 2;
              divisor *= 2;
            }
            buffer[x+y*len[0]+z*(len[0]*len[1])] = var;
          }
        }
      }
      break;
    }case (4):{
      for (size_t w=0; w < len[3]; w++){
        for (size_t z=0; z < len[2]; z++){
          for (size_t y=0; y < len[1]; y++){
            for (size_t x=0; x < len[0]; x++){
              double var = 0;
              int64_t potenz = max_potenz;
              int64_t divisor = 1;
              for(int f = 1 ; f <= frequencyCount; f++){
                var += potenz * open_simplex_noise4(ctx, (double) x / len[0]*divisor*highFrequency, (double) y / len[1]*divisor*highFrequency, (double) z / len[2]*divisor*highFrequency, (double) w / len[3]*divisor*highFrequency);
                potenz /= 2;
                divisor *= 2;
              }
              buffer[x+y*len[0]+z*(len[0]*len[1])+w*(len[0]*len[1]*len[2])] = var;
            }
          }
        }
      }
      break;
    }default:
      assert(0 && "Not supported");
  }

  // fix min + max, first identify min/max
  size_t count = scil_get_data_count(dims);
  double mn_o = 1e308, mx_o=-1e308;
  for (size_t i=0; i < count; i++){
    mn_o = min(mn_o, buffer[i]);
    mx_o = max(mx_o, buffer[i]);
  }

  double scaling = (double)(mx - mn) / (mx_o-mn_o); // intended min/max
  // rescale
  for (size_t i=0; i < count; i++){
    buffer[i] = (double) mn + (buffer[i]-mn_o) *scaling;
  }

  open_simplex_noise_free(ctx);

  return SCIL_NO_ERR;
}

scil_pattern scil_pattern_simplex_noise = { &simplex, "simplexNoise" };