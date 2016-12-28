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

/*
 * This tool will compress / decompress a CSV
 */
 #include <scil.h>
 #include <scil-error.h>
 #include <scil-option.h>
 #include <scil-util.h>

 #include <stdio.h>
 #include <stdlib.h>
 #include <time.h>

 #include <plugins/file-plugin.h>
 #include "cnoise.h"

 /*
  * This is example code for how to call functions from the cnoise library.
  * We generate M realizations of size N of noise
  * With std = 1 and M >= 10000, for cnoise_generate_colored_noise(...) you should get
  * expected value for the mean to be close to zero and the standard deviation should
  * vary linearly in alpha with 1 for alpha = 0 and 0.575 for alpha = 2.
  * Note that the values are approximate.
  *
 */


 static double mean( double *x, int N ){
 	int i;
 	double mean = 0.0;
 	for( i=0; i<N; i++ ){ mean += x[i]; };
 	return ( mean / ( (double) N ) );
 }

 static void mean_std( double *x, int N, double *mean, double *std ){
 	int i;
 	*mean = 0.0;
 	for( i=0; i<N; i++ ){ *mean += x[i]; };
 	*mean /= (double) N;

 	*std = 0.0;
 	for( i=0; i<N; i++ ){ *std += ( x[i] - *mean ) * (x[i] - *mean ); };
 	*std = sqrt( *std /( (double) (N-1) )  );
 }

 int main(){

 	double std = 1.0;
 	double alpha = 1.0;
 	double range = 1.0;
 	int N = 10000;
 	int M = 10000;

 	int i,j;
 	double scale = exp( 0.5*(1-alpha) * log( N ) );
 	double *x = malloc( N * sizeof( double ) );
 	double *res = malloc( M * sizeof( double ) );
 	double result_mean, result_std;


 	srand( time(0) ); // Make sure to call this in your program before you call any of the cnoise_* functions

 	for( i=0; i<M; i++ ){
 		cnoise_generate_colored_noise( x, N, alpha, std );
 		//cnoise_generate_colored_noise_uniform( x, N, alpha, range );
 		//cnoise_generate_colored_noise_truncated( x, N, alpha, std, range );
 		for( j=0; j<N; j++ ){
 			x[j] *= scale;
 		};
 		res[i] = mean( x, N );
 	};

 	mean_std( res, M, &result_mean, &result_std );

 	printf( " Alpha = %f  Std = %f  Range = %f  M = %d  N = %d \n",alpha,std,range,M,N );

 	printf( "   Expected value of the mean %f  Standard deviation %f \n",result_mean,result_std );

 	free( res );
 	free( x );

 	return 0;
 }
