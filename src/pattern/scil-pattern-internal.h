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

#ifndef SCIL_PATTERN_INTERNAL_LIB_H
#define SCIL_PATTERN_INTERNAL_LIB_H

#include <scil-dims.h>

typedef struct {
    int (*create)(double* const buffer, const scil_dims* const dims, float mn, float mx, float arg, float arg2);
  char * name;
} scil_pattern;

typedef void (*scilPa_mutator)(double* const buffer, const scil_dims* const dims, float arg);

void scilPI_fix_min_max(double * buffer, const scil_dims* const dims, float mn, float mx);

#endif
