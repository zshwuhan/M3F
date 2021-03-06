/* 
  M3F_TIB_SAMPLEOFFSETS Gibbs samples the c and d bias matrices
   
  Usage: 
     m3f_tib_sampleOffsets(data, model, samp, zU, zM, resids)
     m3f_tib_sampleOffsets(data, model, samp, zU, zM, resids,
                             [sampUserParams, sampItemParams])

  Inputs: 
     data - Dyadic data structure (see loadDyadicData)
     model - m3f_tib structure (see m3f_tib_initModel)
     samp - Current Gibbs sample of model variables
     zU,zM - sampled user/item latent topics
     resids - differences between true ratings and base rating predictions
     (see m3f_tib_gibbs)
     sampleUserParams - OPTIONAL: if false, user params are not sampled
     sampleItemParams - OPTIONAL: if false, item params are not sampled

  Outputs:
     This function modifies the "samp" structure input IN PLACE, 
     replacing the 'a' and 'b' samples.

  ------------------------------------------------------------------------     

  Last revision: 2-July-2010

  Authors: Lester Mackey and David Weiss
  License: MIT License

  Copyright (c) 2010 Lester Mackey & David Weiss

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:
  
  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

  ------------------------------------------------------------------------
*/

#include <math.h>
#include <stdint.h>
#include <omp.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include "mex.h"
#include "mexCommon.h"
#include "mexUtil.h"

// Sample offsets
// Function written from perspective of sample c offsets
// Switch roles of user-item inputs to sample d offsets
void sampleOffsets(uint32_t* users, uint32_t* items,  const mxArray* exampsByUser,
                   int KU, int KM, int numUsers, double invSigmaSqd,
                   double invSigmaSqd0, double c0, double* c, double* d,
                   uint32_t* zU, uint32_t* zM, double* resids){

   // Array of static random number generators
   gsl_rng** rngs = getRngArray();

   // Extract internals of jagged arrays
   uint32_t** userExamps;
   mwSize* userLens;
   unpackJagged(exampsByUser, &userExamps, &userLens, numUsers);

   // Prior term for offsets
   const double ratio = c0*invSigmaSqd0;

   // Allocate memory for storing topic counts
   int* counts[MAX_NUM_THREADS];
   for(int thread = 0; thread < MAX_NUM_THREADS; thread++)
      counts[thread] = mxMalloc(KM*sizeof(*counts));

#pragma omp parallel for
   for(int u = 0; u < numUsers; u++){
      int thread = omp_get_thread_num();
      // Initialize c offsets to 0
      double* cPtr = c + u*KM;
      fillArrayD(cPtr, KM, 0);

      // Initialize topic counts to 0
      fillArrayI(counts[thread], KM, 0);

      // Iterate over user's examples computing sufficient stats
      mwSize len = userLens[u];
      uint32_t* examps = userExamps[u];
      for(int j = 0; j < len; j++){
         uint32_t e = examps[j]-1;
         int i = zM[e]-1;

         if(KU > 0)
            cPtr[i] += (resids[e] - d[(items[e]-1)*KU + (zU[e]-1)]);
         else
            cPtr[i] += resids[e];
         counts[thread][i]++;
      }

      // Sample new offset values using sufficient stats
      for(int i = 0; i < KM; i++){
         double variance = 1.0/(invSigmaSqd0 + counts[thread][i]*invSigmaSqd);
         cPtr[i] = (ratio + cPtr[i]*invSigmaSqd)*variance +
            gsl_ran_gaussian(rngs[omp_get_thread_num()], sqrt(variance));
      }
   }
   // Clean up
   mxFree(userExamps);
   mxFree(userLens);
   for(int thread = 0; thread < MAX_NUM_THREADS; thread++)
      mxFree(counts[thread]);
}

void mexFunction(int nlhs, mxArray *plhs[],
                 int nrhs, const mxArray *prhs[])
{
   mexPrintf("Running m3f_tib_sampleOffsets\n");

   omp_set_num_threads(MAX_NUM_THREADS);

   // Extract input information
   const mxArray* data = prhs[0];
   uint32_t* users = (uint32_t*)mxGetData(mxGetField(data, 0, "users"));
   uint32_t* items = (uint32_t*)mxGetData(mxGetField(data, 0, "items"));
   const mxArray* exampsByUser = mxGetField(data, 0, "exampsByUser");
   const mxArray* exampsByItem = mxGetField(data, 0, "exampsByItem");
   const mxArray* model = prhs[1];
   int KU = (*mxGetPr(mxGetField(model, 0, "KU"))) + .5;
   int KM = (*mxGetPr(mxGetField(model, 0, "KM"))) + .5;
   int numUsers = (*mxGetPr(mxGetField(model, 0, "numUsers"))) + .5;
   int numItems = (*mxGetPr(mxGetField(model, 0, "numItems"))) + .5;
   double invSigmaSqd = 1.0/(*mxGetPr(mxGetField(model, 0, "sigmaSqd")));
   double invSigmaSqd0 = 1.0/(*mxGetPr(mxGetField(model, 0, "sigmaSqd0")));
   double c0 = (*mxGetPr(mxGetField(model, 0, "c0")));
   double d0 = (*mxGetPr(mxGetField(model, 0, "d0")));
   const mxArray* samp = prhs[2];
   double* c = mxGetPr(mxGetField(samp, 0, "c")); // KM x numUsers
   double* d = mxGetPr(mxGetField(samp, 0, "d")); // KU x numItems
   uint32_t* zU = (uint32_t*)mxGetData(prhs[3]);
   uint32_t* zM = (uint32_t*)mxGetData(prhs[4]);
   double* resids = mxGetPr(prhs[5]);
   mxLogical* sampParams = NULL;
   if(nrhs > 6){
      sampParams = mxGetLogicals(prhs[6]);
   }

   // Sample c offsets
   if((KM > 0) && ((sampParams == NULL) || sampParams[0])){
      sampleOffsets(users, items, exampsByUser, KU, KM, numUsers,
                    invSigmaSqd, invSigmaSqd0, c0, c, d, zU,
                    zM, resids);
   }

   // Sample d offsets
   if((KU > 0) && ((sampParams == NULL) || sampParams[1])){
      sampleOffsets(items, users, exampsByItem, KM, KU, numItems,
                    invSigmaSqd, invSigmaSqd0, d0, d, c, zM,
                    zU, resids);
   }
}
