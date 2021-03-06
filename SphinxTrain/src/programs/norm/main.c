/* ====================================================================
 * Copyright (c) 1995-2000 Carnegie Mellon University.  All rights 
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*********************************************************************
 *
 * File: main.c
 * 
 * Description: 
 *	This normalizes the counts produced by bw(1).
 * 
 * Author: 
 *	Eric H. Thayer (eht@cs.cmu.edu)
 * 
 *********************************************************************/

#include "parse_cmd_ln.h"

#include <s3/feat.h>

/* The SPHINX-III common library */
#include <s3/common.h>

#include <s3/model_inventory.h>
#include <s3/model_def_io.h>
#include <s3/s3gau_io.h>
#include <s3/s3regmat_io.h>
#include <s3/s3mixw_io.h>
#include <s3/s3tmat_io.h>
#include <s3/s3acc_io.h>
#include <s3/regmat_io.h>
#include <s3/matrix.h>

/* Some SPHINX-II compatibility definitions */
#include <s3/s2_param.h>

#include <s2/log.h>

#include <sys_compat/file.h>
#include <sys_compat/misc.h>

#include <stdio.h>
#include <math.h>
#include <assert.h>

#include <s3/mllr.h>
#include <s3/mllr_io.h>

static int normalize(void);


/* the following function is used for MMIE training
   lqin 2010-03 */
static int mmi_normalize(void);
/* end */

static int
initialize(int argc,
	   char *argv[])
{

    /* define, parse and (partially) validate the command line */
    parse_cmd_ln(argc, argv);

    return S3_SUCCESS;
}


static int
normalize()
{
    char file_name[MAXPATHLEN+1];
    float32 ***mixw_acc = NULL;
    float32 ***in_mixw = NULL;
    float64 s;
    uint32 n_mixw;
    uint32 n_stream;
    uint32 n_mllr_class;
    uint32 n_density;
    float32 ***tmat_acc = NULL;
    uint32 n_tmat;
    uint32 n_state_pm;
    uint32 i, j, k;
    vector_t ***in_mean = NULL;
    vector_t ***wt_mean = NULL;
    vector_t ***in_var = NULL;
    vector_t ***wt_var = NULL;
    vector_t ****in_fullvar = NULL;
    vector_t ****wt_fullvar = NULL;
    int32 pass2var = FALSE;
    int32 var_is_full = FALSE;
    float32 ***dnom = NULL;
    uint32 n_mgau;
    uint32 n_gau_stream;
    uint32 n_gau_density;
    const uint32 *veclen = NULL;
    const char **accum_dir;
    const char *oaccum_dir;
    const char *in_mixw_fn;
    const char *out_mixw_fn;
    const char *out_tmat_fn;
    const char *in_mean_fn;
    const char *out_mean_fn;
    const char *in_var_fn;
    const char *out_var_fn;
    const char *out_dcount_fn;
    int err;
    uint32 mllr_mult;
    uint32 mllr_add;
    float32 *****regl = NULL;
    float32 ****regr = NULL;
    float32 ****A;
    float32 ***B; 
    uint32 no_retries=0;

    
    accum_dir = (const char **)cmd_ln_access("-accumdir");
    oaccum_dir = (const char *)cmd_ln_access("-oaccumdir");

    out_mixw_fn = (const char *)cmd_ln_access("-mixwfn");
    out_tmat_fn = (const char *)cmd_ln_access("-tmatfn");
    out_mean_fn = (const char *)cmd_ln_access("-meanfn");
    out_var_fn = (const char *)cmd_ln_access("-varfn");
    in_mixw_fn = (const char *)cmd_ln_access("-inmixwfn");
    in_mean_fn = (const char *)cmd_ln_access("-inmeanfn");
    in_var_fn = (const char *)cmd_ln_access("-invarfn");
    out_dcount_fn = (const char *)cmd_ln_access("-dcountfn");
    var_is_full = cmd_ln_int32("-fullvar");

    /* must be at least one accum dir */
    assert(accum_dir[0] != NULL);

    if (out_mixw_fn == NULL) {
	E_INFO("No -mixwfn specified, will skip if any\n");
    }
    if (out_tmat_fn == NULL) {
	E_INFO("No -tmatfn specified, will skip if any\n");
    }
    if (out_mean_fn == NULL) {
	E_INFO("No -meanfn specified, will skip if any\n");
    }
    if (out_var_fn == NULL) {
	E_INFO("No -varfn specified, will skip if any\n");
    }
    if (in_mixw_fn != NULL) {
	E_INFO("Selecting unseen mixing weight parameters from %s\n",
	       in_mixw_fn);
    }

    if (in_mean_fn != NULL) {
	E_INFO("Selecting unseen density mean parameters from %s\n",
	       in_mean_fn);

	if (s3gau_read(in_mean_fn,
		       &in_mean,
		       &n_mgau,
		       &n_gau_stream,
		       &n_gau_density,
		       &veclen) != S3_SUCCESS) {
	  E_FATAL_SYSTEM("Couldn't read %s", in_mean_fn);
	}
	ckd_free((void *)veclen);
	veclen = NULL;
    }

    if (in_var_fn != NULL) {
	E_INFO("Selecting unseen density variance parameters from %s\n",
	       in_var_fn);

	if (var_is_full) {
	    if (s3gau_read_full(in_var_fn,
			   &in_fullvar,
			   &n_mgau,
			   &n_gau_stream,
			   &n_gau_density,
			   &veclen) != S3_SUCCESS) {
		E_FATAL_SYSTEM("Couldn't read %s", in_var_fn);
	    }
	}
	else {
	    if (s3gau_read(in_var_fn,
			   &in_var,
			   &n_mgau,
			   &n_gau_stream,
			   &n_gau_density,
			   &veclen) != S3_SUCCESS) {
		E_FATAL_SYSTEM("Couldn't read %s", in_var_fn);
	    }
	}
	ckd_free((void *)veclen);
	veclen = NULL;
    }

    n_stream = 0;
    for (i = 0; accum_dir[i]; i++) {
	E_INFO("Reading and accumulating counts from %s\n", accum_dir[i]);

	if (out_mixw_fn) {
	    rdacc_mixw(accum_dir[i],
		       &mixw_acc, &n_mixw, &n_stream, &n_density);
	}

	if (out_tmat_fn) {
	    rdacc_tmat(accum_dir[i],
		       &tmat_acc, &n_tmat, &n_state_pm);
	}

	if (out_mean_fn || out_var_fn) {
	    if (var_is_full)
		rdacc_den_full(accum_dir[i],
			       &wt_mean,
			       &wt_fullvar,
			       &pass2var,
			       &dnom,
			       &n_mgau,
			       &n_gau_stream,
			       &n_gau_density,
			       &veclen);
	    else
		rdacc_den(accum_dir[i],
			  &wt_mean,
			  &wt_var,
			  &pass2var,
			  &dnom,
			  &n_mgau,
			  &n_gau_stream,
			  &n_gau_density,
			  &veclen);

	    if (out_mixw_fn) {
		if (n_stream != n_gau_stream) {
		    E_ERROR("mixw inconsistent w/ densities WRT # "
			    "streams (%u != %u)\n",
			    n_stream, n_gau_stream);
		}

		if (n_density != n_gau_density) {
		    E_ERROR("mixw inconsistent w/ densities WRT # "
			    "den/mix (%u != %u)\n",
			    n_density, n_gau_density);
		}
	    }
	    else {
		n_stream = n_gau_stream;
		n_density = n_gau_density;
	    }
	}
    }

    if (oaccum_dir && mixw_acc) {
	/* write the total mixing weight reest. accumulators */

	err = 0;
	sprintf(file_name, "%s/mixw_counts", oaccum_dir);

	if (in_mixw_fn) {
	    if (s3mixw_read(in_mixw_fn,
			    &in_mixw,
			    &i,
			    &j,
			    &k) != S3_SUCCESS) {
		E_FATAL_SYSTEM("Unable to read %s", in_mixw_fn);
	    }
	    if (i != n_mixw) {
		E_FATAL("# mixw in input mixw file != # mixw in output mixw file\n");
	    }
	    if (j != n_stream) {
		E_FATAL("# stream in input mixw file != # stream in output mixw file\n");
	    }
	    if (k != n_density) {
		E_FATAL("# density in input mixw file != # density in output mixw file\n");
	    }
	    
	    for (i = 0; i < n_mixw; i++) {
		for (j = 0; j < n_stream; j++) {
		    for (k = 0, s = 0; k < n_density; k++) {
			s += mixw_acc[i][j][k];
		    }
		    if ((s == 0) && in_mixw) {
			for (k = 0, s = 0; k < n_density; k++) {
			    mixw_acc[i][j][k] = in_mixw[i][j][k];
			}
			E_INFO("set mixw %u stream %u to input mixw value\n", i, j);
		    }
		}
	    }
	}

	do {
	    /* Write out the accumulated reestimation sums */
	    if (s3mixw_write(file_name,
			     mixw_acc,
			     n_mixw,
			     n_stream,
			     n_density) != S3_SUCCESS) {
		if (err == 0) {
		    E_ERROR("Unable to write %s; Retrying...\n", file_name);
		}
		++err;
		sleep(3);
		no_retries++;
		if(no_retries>10){ 
		  E_FATAL("Failed to get the files after 10 retries(about 30 seconds).\n ");
		}
	    }
	} while (err > 1);
    }

    if (pass2var)
	    E_INFO("-2passvar yes\n");
    if (oaccum_dir && (wt_mean || wt_var || wt_fullvar)) {
	/* write the total mixing Gau. den reest. accumulators */

	err = 0;
	sprintf(file_name, "%s/gauden_counts", oaccum_dir);
	do {
	    int32 rv;

	    if (var_is_full)
		rv = s3gaucnt_write_full(file_name,
					 wt_mean,
					 wt_fullvar,
					 pass2var,
					 dnom,
					 n_mgau,
					 n_gau_stream,
					 n_gau_density,
					 veclen);
	    else
		rv = s3gaucnt_write(file_name,
				    wt_mean,
				    wt_var,
				    pass2var,
				    dnom,
				    n_mgau,
				    n_gau_stream,
				    n_gau_density,
				    veclen);
		
	    if (rv != S3_SUCCESS) {
		if (err == 0) {
		    E_ERROR("Unable to write %s; Retrying...\n", file_name);
		}
		++err;
		sleep(3);
		no_retries++;
		if(no_retries>10){ 
		  E_FATAL("Failed to get the files after 10 retries(about 5 minutes).\n ");
		}
	    }
	} while (err > 1);
    }

    if (oaccum_dir && tmat_acc) {
	/* write the total transition matrix reest. accumulators */

	err = 0;
	sprintf(file_name, "%s/tmat_counts", oaccum_dir);
	do {
	    if (s3tmat_write(file_name,
			     tmat_acc,
			     n_tmat,
			     n_state_pm) != S3_SUCCESS) {
		if (err == 0) {
		    E_ERROR("Unable to write %s; Retrying...\n", file_name);
		}
		++err;
		sleep(3);
		no_retries++;
		if(no_retries>10){ 
		  E_FATAL("Failed to get the files after 10 retries(about 5 minutes).\n ");
		}
	    }
	} while (err > 1);
    }

    if (oaccum_dir && regr && regl) {
	/* write the total MLLR regression matrix accumulators */

	err = 0;
	sprintf(file_name, "%s/regmat_counts", oaccum_dir);
	do {
	    if (s3regmatcnt_write(file_name,
				  regr,
				  regl,
				  n_mllr_class,
				  n_stream,
				  veclen,
				  mllr_mult,
				  mllr_add) != S3_SUCCESS) {
		if (err == 0) {
		    E_ERROR("Unable to write %s; Retrying...\n", file_name);
		}
		++err;
		sleep(3);
		no_retries++;
		if(no_retries>10){ 
		  E_FATAL("Failed to get the files after 10 retries(about 5 minutes).\n ");
		}
	    }
	} while (err > 1);
    }

    if (wt_mean || wt_var || wt_fullvar) {
	if (out_mean_fn) {
	    E_INFO("Normalizing mean for n_mgau= %u, n_stream= %u, n_density= %u\n",
		   n_mgau, n_stream, n_density);

	    gauden_norm_wt_mean(in_mean, wt_mean, dnom,
				n_mgau, n_stream, n_density, veclen);
	}
	else {
	    if (wt_mean) {
		E_INFO("Ignoring means since -meanfn not specified\n");
	    }
	}

	if (out_var_fn) {
	    if (var_is_full) {
		if (wt_fullvar) {
		    E_INFO("Normalizing fullvar\n");
		    gauden_norm_wt_fullvar(in_fullvar, wt_fullvar, pass2var, dnom,
					   wt_mean,	/* wt_mean now just mean */
					   n_mgau, n_stream, n_density, veclen,
					   cmd_ln_boolean("-tiedvar"));
		}
	    }
	    else {
		if (wt_var) {
		    E_INFO("Normalizing var\n");
		    gauden_norm_wt_var(in_var, wt_var, pass2var, dnom,
				       wt_mean,	/* wt_mean now just mean */
				       n_mgau, n_stream, n_density, veclen,
				       cmd_ln_boolean("-tiedvar"));
		}
	    }
	}
	else {
	    if (wt_var || wt_fullvar) {
		E_INFO("Ignoring variances since -varfn not specified\n");
	    }
	}
    }
    else {
	E_INFO("No means or variances to normalize\n");
    }

    /*
     * Write the parameters to files
     */

    if (out_mixw_fn) {
	if (mixw_acc) {
	    if (s3mixw_write(out_mixw_fn,
			     mixw_acc,
			     n_mixw,
			     n_stream,
			     n_density) != S3_SUCCESS) {
		return S3_ERROR;
	    }
	}
	else {
	    E_WARN("NO mixing weight accumulators seen, but -mixwfn specified.\n");
	}
    }
    else {
	if (mixw_acc) {
	    E_INFO("Mixing weight accumulators seen, but -mixwfn NOT specified.\n");
	}
    }

    if (out_tmat_fn) {
	if (tmat_acc) {
	    if (s3tmat_write(out_tmat_fn,
			     tmat_acc,
			     n_tmat,
			     n_state_pm) != S3_SUCCESS) {
		return S3_ERROR;
	    }
	}
	else {
	    E_WARN("NO transition matrix accumulators seen, but -tmatfn specified.\n");
	}
    }
    else {
	if (tmat_acc) 
	    E_INFO("Transition matrix accumulators seen, but -tmatfn NOT specified\n");
    }

    
    if (out_mean_fn) {
	if (wt_mean) {
	    if (s3gau_write(out_mean_fn,
			    (const vector_t ***)wt_mean,
			    n_mgau,
			    n_stream,
			    n_density,
			    veclen) != S3_SUCCESS)
		return S3_ERROR;
	    
	    if (out_dcount_fn) {
		if (s3gaudnom_write(out_dcount_fn,
				    dnom,
				    n_mgau,
				    n_stream,
				    n_density) != S3_SUCCESS)
		    return S3_ERROR;
	    }
	}
	else
	    E_WARN("NO reestimated means seen, but -meanfn specified\n");
    }
    else {
	if (wt_mean) {
	    E_INFO("Reestimated means seen, but -meanfn NOT specified\n");
	}
    }
    
    if (out_var_fn) {
	if (var_is_full) {
	    if (wt_fullvar) {
		if (s3gau_write_full(out_var_fn,
				     (const vector_t ****)wt_fullvar,
				     n_mgau,
				     n_stream,
				     n_density,
				     veclen) != S3_SUCCESS)
		    return S3_ERROR;
	    }
	    else
		E_WARN("NO reestimated variances seen, but -varfn specified\n");
	}
	else {
	    if (wt_var) {
		if (s3gau_write(out_var_fn,
				(const vector_t ***)wt_var,
				n_mgau,
				n_stream,
				n_density,
				veclen) != S3_SUCCESS)
		    return S3_ERROR;
	    }
	    else
		E_WARN("NO reestimated variances seen, but -varfn specified\n");
	}
    }
    else {
	if (wt_var) {
	    E_INFO("Reestimated variances seen, but -varfn NOT specified\n");
	}
    }

    if (veclen)
	ckd_free((void *)veclen);

    return S3_SUCCESS;
}


/* the following function is used for MMIE training
   lqin 2010-03 */
static int
mmi_normalize()
{
  uint32 i;
  
  uint32 n_mgau;
  uint32 n_stream;
  uint32 n_density;
  vector_t ***in_mean = NULL;
  vector_t ***in_var = NULL;
  vector_t ***wt_mean = NULL;
  vector_t ***wt_var = NULL;
  const uint32 *veclen = NULL;
  
  const char **accum_dir;
  const char *in_mean_fn;
  const char *out_mean_fn;
  const char *in_var_fn;
  const char *out_var_fn;
  
  vector_t ***wt_num_mean = NULL;
  vector_t ***wt_den_mean = NULL;
  vector_t ***wt_num_var = NULL;
  vector_t ***wt_den_var = NULL;
  float32 ***num_dnom = NULL;
  float32 ***den_dnom = NULL;
  uint32 n_num_mgau;
  uint32 n_den_mgau;
  uint32 n_num_stream;
  uint32 n_den_stream;
  uint32 n_num_density;
  uint32 n_den_density;
  
  float32 constE;
  
  uint32 n_temp_mgau;
  uint32 n_temp_stream;
  uint32 n_temp_density;
  const uint32 *temp_veclen = NULL;
  
  accum_dir = (const char **)cmd_ln_access("-accumdir");
  
  /* the following variables are used for mmie training */
  out_mean_fn = (const char *)cmd_ln_access("-meanfn");
  out_var_fn = (const char *)cmd_ln_access("-varfn");
  in_mean_fn = (const char *)cmd_ln_access("-inmeanfn");
  in_var_fn = (const char *)cmd_ln_access("-invarfn");
  constE = cmd_ln_float32("-constE");
  
  /* get rid of some unnecessary parameters */
  if (cmd_ln_int32("-fullvar")) {
    E_FATAL("Current MMIE training can not be done for full variance, set -fulllvar as no\n");
  }
  if (cmd_ln_int32("-tiedvar")) {
    E_FATAL("Current MMIE training can not be done for tied variance, set -tiedvar as no\n");
  }
  if (cmd_ln_access("-mixwfn")) {
    E_FATAL("Current MMIE training does not support mixture weight update, remove -mixwfn \n");
  }
  if (cmd_ln_access("-inmixwfn")) {
    E_FATAL("Current MMIE training does not support mixture weight update, remove -inmixwfn \n");
  }
  if (cmd_ln_access("-tmatfn")) {
    E_FATAL("Current MMIE training does not support transition matrix update, remove -tmatfn \n");
  }
  if (cmd_ln_access("-regmatfn")) {
    E_FATAL("Using norm for computing regression matrix is obsolete, please use mllr_transform \n");
  }
  
  /* must be at least one accum dir */
  if (accum_dir[0] == NULL) {
    E_FATAL("No accumulated reestimation path is specified, use -accumdir \n");
  }
  
  /* at least update mean or variance parameters */
  if (out_mean_fn == NULL && out_var_fn == NULL) {
    E_FATAL("Neither -meanfn nor -varfn is specified, at least do mean or variance update \n");
  }
  else if (out_mean_fn == NULL) {
    E_INFO("No -meanfn specified, will skip if any\n");
  }
  else if (out_var_fn == NULL) {
    E_INFO("No -varfn specified, will skip if any\n");
  }
  
  /* read input mean */
  if (in_mean_fn != NULL) {
    E_INFO("read original density mean parameters from %s\n", in_mean_fn);
    if (s3gau_read(in_mean_fn,
		   &in_mean,
		   &n_mgau,
		   &n_stream,
		   &n_density,
		   &veclen) != S3_SUCCESS) {
      E_FATAL_SYSTEM("Couldn't read %s", in_mean_fn);
    }
    ckd_free((void *)veclen);
    veclen = NULL;
  }

  /* read input variance */
  if (in_var_fn != NULL) {
    E_INFO("read original density variance parameters from %s\n", in_var_fn);
    if (s3gau_read(in_var_fn,
		   &in_var,
		   &n_mgau,
		   &n_stream,
		   &n_density,
		   &veclen) != S3_SUCCESS) {
      E_FATAL_SYSTEM("Couldn't read %s", in_var_fn);
    }
    ckd_free((void *)veclen);
    veclen = NULL;
  }
  
  /* read accumulated numerator and denominator counts */
  for (i = 0; accum_dir[i]; i++) {
    E_INFO("Reading and accumulating counts from %s\n", accum_dir[i]);
    
    rdacc_mmie_den(accum_dir[i],
		   "numlat",
		   &wt_num_mean,
		   &wt_num_var,
		   &num_dnom,
		   &n_num_mgau,
		   &n_num_stream,
		   &n_num_density,
		   &veclen);
    
    rdacc_mmie_den(accum_dir[i],
		   "denlat",
		   &wt_den_mean,
		   &wt_den_var,
		   &den_dnom,
		   &n_den_mgau,
		   &n_den_stream,
		   &n_den_density,
		   &veclen);
    
    if (n_num_mgau != n_den_mgau)
      E_FATAL("number of gaussians inconsistent between num and den lattice\n");
    else if (n_num_mgau != n_mgau)
      E_FATAL("number of gaussians inconsistent between imput model and accumulator (%u != %u)\n", n_mgau, n_num_mgau);
    
    if (n_num_stream != n_den_stream)
      E_FATAL("number of gaussian streams inconsistent between num and den lattice\n");
    else if (n_num_stream != n_stream)
      E_FATAL("number of gaussian streams inconsistent between imput model and accumulator (%u != %u)\n", n_stream, n_num_stream);
    
    if (n_num_density != n_den_density)
      E_FATAL("number of gaussian densities inconsistent between num and den lattice\n");
    else if (n_num_density != n_density)
      E_FATAL("number of gaussian densities inconsistent between imput model and accumulator (%u != %u)\n", n_density, n_num_density);
  }
  
  /* initialize update parameters as the input parameters */
  if (out_mean_fn) {
    if (s3gau_read(in_mean_fn,
		   &wt_mean,
		   &n_temp_mgau,
		   &n_temp_stream,
		   &n_temp_density,
		   &temp_veclen) != S3_SUCCESS) {
      E_FATAL_SYSTEM("Couldn't read %s", in_mean_fn);
    }
    ckd_free((void *)temp_veclen);
    temp_veclen = NULL;
  }

  if (out_var_fn) {
    if (s3gau_read(in_var_fn,
		   &wt_var,
		   &n_temp_mgau,
		   &n_temp_stream,
		   &n_temp_density,
		   &temp_veclen) != S3_SUCCESS) {
      E_FATAL_SYSTEM("Couldn't read %s", in_var_fn);
    }
    ckd_free((void *)temp_veclen);
    temp_veclen = NULL;
  }
  
  /* update mean parameters */
  if (wt_mean) {
    if (out_mean_fn) {
      E_INFO("Normalizing mean for n_mgau= %u, n_stream= %u, n_density= %u\n",
	     n_mgau, n_stream, n_density);
      
      gauden_norm_wt_mmie_mean(in_mean, wt_mean, wt_num_mean, wt_den_mean,
			       in_var, wt_num_var, wt_den_var, num_dnom, den_dnom,
			       n_mgau, n_stream, n_density, veclen, constE);
    }
    else {
      E_INFO("Ignoring means since -meanfn not specified\n");
    }
  }
  else {
    E_INFO("No means to normalize\n");
  }
  
  /* update variance parameters */
  if (wt_var) {
    if (out_var_fn) {
      E_INFO("Normalizing variance for n_mgau= %u, n_stream= %u, n_density= %u\n",
	     n_mgau, n_stream, n_density);
      
      gauden_norm_wt_mmie_var(in_var, wt_var, wt_num_var, wt_den_var, num_dnom, den_dnom,
			      in_mean, wt_mean, wt_num_mean, wt_den_mean,
			      n_mgau, n_stream, n_density, veclen, constE);
    }
    else {
      E_INFO("Ignoring variances since -varfn not specified\n");
    }
  }
  else {
    E_INFO("No variances to normalize\n");
  }
  
  /* write the updated mean parameters to files */
  if (out_mean_fn) {
    if (wt_mean) {
      if (s3gau_write(out_mean_fn,
		      (const vector_t ***)wt_mean,
		      n_mgau,
		      n_stream,
		      n_density,
		      veclen) != S3_SUCCESS) {
	return S3_ERROR;
      }
    }
    else {
      E_WARN("NO reestimated means seen, but -meanfn specified\n");
    }
  }
  else {
    if (wt_mean) {
      E_INFO("Reestimated means seen, but -meanfn NOT specified\n");
    }
  }
  
  /* write the updated variance parameters to files */
  if (out_var_fn) {
    if (wt_var) {
      if (s3gau_write(out_var_fn,
		      (const vector_t ***)wt_var,
		      n_mgau,
		      n_stream,
		      n_density,
		      veclen) != S3_SUCCESS) {
	return S3_ERROR;
      }
    }
    else {
      E_WARN("NO reestimated variances seen, but -varfn specified\n");
    }
  }
  else {
    if (wt_var) {
      E_INFO("Reestimated variances seen, but -varfn NOT specified\n");
    }
  }
  
  if (veclen)
    ckd_free((void *)veclen);
  
  if (temp_veclen)
    ckd_free((void *)temp_veclen);
  
  return S3_SUCCESS;
}
/* end */

/* the main() function is modified for MMIE training
   lqin 2010-03 */
int
main(int argc, char *argv[])
{
    if (initialize(argc, argv) != S3_SUCCESS) {
	E_FATAL("errors initializing.\n");
    }

    if (cmd_ln_int32("-mmie")) {
      if (mmi_normalize() != S3_SUCCESS) {
	E_FATAL("errors mmie normalizing.\n");
      }
    }
    else {
      if (normalize() != S3_SUCCESS) {
	E_FATAL("errors normalizing.\n");
      }
    }

    return 0;
}
/* end */

/*
 * Log record.  Maintained by RCS.
 *
 * $Log$
 * Revision 1.11  2006/02/06  13:05:04  eht
 * Moved reading of input mean/var out of the loop that reads and accumulates
 * counts because it doesn't need to be there.  Also, since veclen was freed
 * after reading in the input means/vars, its value was potentially
 * corrupted for later executed code (i.e. further accumulation and mean/var save).
 * 
 * Revision 1.10  2004/11/17 01:46:59  arthchan2003
 * Change the sleeping time to be at most 30 seconds. No one will know whether the code dies or not if keep the code loop infinitely.
 *
 * Revision 1.9  2004/08/19 22:24:14  arthchan2003
 * Fixing numerical problem of compute_mllr and mllr_solve.  There are small numerical differences between the inputs typed with float64 or float32. In terms of (<6 signficiant digits).  This small difference will translate to perceivable numerical difference in the final matrix. (>5 significant digits).  This fix also marks a stable release for mllr_solve.  I also disallow user to use -regmat in norm because it is highly dangerous and known to be slow and didn't help too much.
 *
 * Revision 1.8  2004/07/27 12:07:31  arthchan2003
 * Check-in mllr_solve, a program that can compute the adaptation matrix. There is still some precision problem at this point. But it is good enough to check-in
 *
 * Revision 1.7  2004/07/21 22:32:27  egouvea
 * Fixed some compatibility issues between platforms: make sure we open
 * files with "wb" or "rb", move some #include not defined for all
 * platforms to the proper #if defined() etc.
 *
 * Revision 1.6  2004/07/21 22:00:44  egouvea
 * Changed the license terms to make it the same as sphinx2 and sphinx3.
 *
 * Revision 1.5  2002/05/16 21:07:14  egouvea
 * norm was requesting some parameters that it doesn't really need, like
 * feature string definition and size of input vector. Removed the request.
 *
 * Revision 1.4  2001/04/05 20:02:31  awb
 * *** empty log message ***
 *
 * Revision 1.3  2001/03/01 00:47:44  awb
 * *** empty log message ***
 *
 * Revision 1.2  2000/09/29 22:35:14  awb
 * *** empty log message ***
 *
 * Revision 1.1  2000/09/24 21:38:31  awb
 * *** empty log message ***
 *
 * Revision 1.16  97/07/16  11:22:03  eht
 * Some changes needed by reorganization of library routines
 * 
 * Revision 1.15  97/03/07  08:46:26  eht
 * - deal w/ change from -veclen -> -ceplen
 * - deal w/ new i/o routines
 * - deal w/ multi-class MLLR changes
 * - fix seg fault bug when not reestimating means/vars
 * 
 * Revision 1.14  1996/09/12  18:13:58  eht
 * Bixa & Vipul MLLR changes
 *
 * Revision 1.12  1996/03/25  15:25:06  eht
 * Deal w/ varying input feature (e.g. MFCC) vector length
 *
 * Revision 1.11  1996/01/26  17:40:51  eht
 * Use new feature module
 * Have gauden() either store the current veclen or get it from
 * feat module.
 *
 * Revision 1.10  1995/12/15  18:37:07  eht
 * Added some type cases for memory alloc/free
 *
 * Revision 1.9  1995/12/14  20:15:43  eht
 * Made info output prose a little more clear.
 *
 * Revision 1.8  1995/11/30  21:03:11  eht
 * Added warning messages if command line says to write a parameter
 * file, but no corresponding count file existed.
 *
 * Revision 1.7  1995/11/16  21:15:31  eht
 * Fixed some bugs for case when no transition matrix
 * counts are gathered
 *
 * Revision 1.6  1995/10/12  18:28:28  eht
 * Get log.h from <s2/log.h>
 *
 * Revision 1.5  1995/10/10  13:09:40  eht
 * Changed to use <s3/prim_type.h>
 *
 * Revision 1.4  1995/10/09  15:29:21  eht
 * Removed __FILE__, __LINE__ arguments to ckd_alloc stuff
 *
 * Revision 1.3  1995/08/09  20:36:29  eht
 * Export normalization of gaussian density to gauden.c
 *
 * Revision 1.2  1995/06/20  12:05:19  eht
 * Removed output of dnom
 *
 * Revision 1.1  1995/06/02  20:35:38  eht
 * Initial revision
 *
 *
 */
