/* -*- c++ -*- */
/* 
 * Copyright 2015 <+YOU OR YOUR COMPANY+>.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "dddc_impl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/*
  res = a*res
 */
void complex_mul_d(complex_double *a, complex_double *res)
{
  double tmp;
  tmp = res->re;
  res->re = a->re*tmp - a->im*res->im;
  res->im = a->im*tmp + a->re*res->im;
}
/*
  res = res + a
 */
void complex_add_d(complex_double *a, complex_double *res){
  res->re = res->re + a->re;
  res->im = res->im + a->im;
}
/*
  res = res + a
  using kahan summation *c
 */
void complex_add_kahan_d(complex_double *a, 
			 complex_double *c, 
			 complex_double *res)
{
  double yr,yi;
  double tr,ti;
  yr = a->re - c->re;
  yi = a->im - c->im;

  tr = res->re + yr;
  ti = res->im + yi;
  
  c->re = (tr - res->re) - yr;
  c->im = (ti - res->im) - yi;
  
  res->re = tr;
  res->im = ti;
}

/*
  res = a*res
 */
void complex_mul_re_d(double a, complex_double *res){
  res->re = res->re*a;
  res->im = res->im*a;
}

double complex_abs_d(complex_double *res){
  return(sqrt(res->re*res->re + res->im *res->im));
}

namespace gr {
  namespace drf {
    dddc::sptr dddc::make(char *filter_file, int len, double f0, double f1, int n, double sr)
    {
      return gnuradio::get_initial_sptr
        (new dddc_impl(filter_file, len, f0, f1, n, sr));
    }

    /*
     * The private constructor
     */
    dddc_impl::dddc_impl(char *filter_file, int len, double f0, double f1, int n, double sr)
      : gr::sync_block("dddc",
              gr::io_signature::make(1, 1, 4),
              gr::io_signature::make(0, 0, 0))
    {
      n_out=n;
      win_len = len;
      dsin0 = (complex_double *)malloc(sizeof(complex_double)*win_len);
      dsin1 = (complex_double *)malloc(sizeof(complex_double)*win_len);
      output0 = (complex_double *)malloc(sizeof(complex_double)*n_out);
      output1 = (complex_double *)malloc(sizeof(complex_double)*n_out);
      double *coef = (double *)malloc(sizeof(double)*len);
      sample_rate = sr;
      sample_idx=0;
      win_idx;
      cf0=f0;
      cf1=f1;

      file_idx=0;
      output_idx=0;
      win_idx=0;
      first=1;

      FILE *f = fopen(filter_file,"rb");
      int read = fread(coef, sizeof(double), len, f);
      fclose(f);

      for(int i=0 ; i<win_len ; i++)
      {
        dsin0[i].re = coef[i]*cos(2.0*M_PI*cf0*((double)i)/sample_rate);
        dsin0[i].im = coef[i]*sin(2.0*M_PI*cf0*((double)i)/sample_rate);
        dsin1[i].re = coef[i]*cos(2.0*M_PI*cf1*((double)i)/sample_rate);
        dsin1[i].im = coef[i]*sin(2.0*M_PI*cf1*((double)i)/sample_rate);
      }
      phase0.re = 1.0;
      phase0.im = 0.0;
      phase1.re = 1.0;
      phase1.im = 0.0;
      // phase rotator
      phase0_rot.re = cos(2.0*M_PI*cf0*((double)win_len)/sample_rate);
      phase0_rot.im = sin(2.0*M_PI*cf0*((double)win_len)/sample_rate); 
      phase1_rot.re = cos(2.0*M_PI*cf1*((double)win_len)/sample_rate); 
      phase1_rot.im = sin(2.0*M_PI*cf1*((double)win_len)/sample_rate); 

      // Kahan's summation compensation
      comp0.re=0.0;
      comp0.im=0.0;
      comp1.re=0.0;
      comp1.im=0.0;

      n_dc=0;
      dc0=0.0;
      dc1=0.0;
      local_index=0;

      tfile = fopen("timestamps.log","w");
    }

    void dddc_impl::get_rx_time(int n)
    {
      struct timeval tv;

      std::vector<gr::tag_t> rx_time_tags;
      get_tags_in_range(rx_time_tags, 0, 0, n, pmt::string_to_symbol("rx_time"));
      
      double t0_frac; 
      uint64_t t0_sec;
      
      //print all tags
      BOOST_FOREACH(const gr::tag_t &rx_time_tag, rx_time_tags)
      {
        const uint64_t offset = rx_time_tag.offset;
        const pmt::pmt_t &value = rx_time_tag.value;

        t0_sec = pmt::to_uint64(pmt::tuple_ref(value, 0));
        t0_frac = pmt::to_double(pmt::tuple_ref(value, 1));
        t0 = (uint64_t)(sample_rate*t0_sec);

	_t0 = (uint64_t)(((uint64_t)sample_rate)*((uint64_t)t0_sec)+((uint64_t)(sample_rate*t0_frac)));
	//        ut0 = ((__float128)t0_sec) + (__float128)t0_frac;

	printf("offset0 %lu t0 %" PRIu64 " t0 %" PRIu64 "\n",offset,t0,t0_sec);
      }
    }

    int dddc_impl::detect_overflow(uint64_t start, uint64_t end)
    {
          uint64_t dt;
      int dropped;
      dropped=0;
      std::vector<gr::tag_t> rx_time_tags;
      get_tags_in_range(rx_time_tags, 0, start, end, pmt::string_to_symbol("rx_time"));
      
      //print all tags
      BOOST_FOREACH(const gr::tag_t &rx_time_tag, rx_time_tags)
      {
        const uint64_t offset = rx_time_tag.offset;
        const pmt::pmt_t &value = rx_time_tag.value;
        
        uint64_t tt0_sec = pmt::to_uint64(pmt::tuple_ref(value, 0));
        double tt0_frac = pmt::to_double(pmt::tuple_ref(value, 1));
        double tt0 = ((double)tt0_sec) + tt0_frac;
	
	// we should have this many samples
	dt = (((int64_t)sample_rate)*tt0_sec + (int64_t)(tt0_frac*sample_rate) - (int64_t)_t0) - (int64_t)total_dropped;

	dropped = dt  - offset;
	total_dropped += dropped;
	printf("Dropped packet. %lu total_dropped %d dropped %u index %d.\n",offset,(int)total_dropped,(int)dropped,(int)(offset-start));
      }
      return(dropped);
    }



    /*
     * Our virtual destructor.
     */
    dddc_impl::~dddc_impl()
    {
    }

    void dddc_impl::consume_samples(short *in, int noutput_items)
    {
      complex_double s0;
      complex_double s1;
      char fname[4096];
      FILE *f;
      int result;
      double scale = 1.0/16384.0;
      struct timeval tv;

      for(int i=0 ; i < noutput_items ; i++)
      {
	/* 
	   if we are at the beginning of the acquisition, 
	   perform averaging to estimate DC offset.
	*/
	if( sample_idx >= JUHA_DDDC_NDC_0 && sample_idx < JUHA_DDDC_NDC_1)
	{
	  dc0 += (double)in[2*i]*scale;
	  dc1 += (double)in[2*i+1]*scale;
	} 
	/* 
	   calculate dc offset when we have enough samples
	 */
	if( sample_idx == JUHA_DDDC_NDC_1 )
	{
	  dc0 = dc0/((double)(JUHA_DDDC_NDC_1 - JUHA_DDDC_NDC_0));
	  dc1 = dc1/((double)(JUHA_DDDC_NDC_1 - JUHA_DDDC_NDC_0));
	  printf("dc offset determined %1.2f %1.2f\n",dc0,dc1);
	}

	// downconvert with double precision, s0 = channel 1 and s1 = channel 2
	s0.re = (double)in[2*i]*scale - dc0;
	s0.im = 0.0;
	s1.re = (double)in[2*i+1]*scale - dc1;
	s1.im = 0.0;

	complex_mul_d(&phase0, &s0);
	// windowed down conversion
	complex_mul_d(&dsin0[win_idx], &s0);
	// 
	complex_add_kahan_d(&s0, &comp0, &output0[output_idx]);
	
	complex_mul_d(&phase1, &s1);
	complex_mul_d(&dsin1[win_idx], &s1);
	// add and multiply
	complex_add_kahan_d(&s1, &comp1, &output1[output_idx]);
	
	win_idx++;
	sample_idx++;
	
	if(win_idx == win_len)
        {
	  win_idx=0;
	  complex_mul_d(&phase0_rot, &phase0);
	  complex_mul_d(&phase1_rot, &phase1);
	  
	  // stabilize phasor by scaling 
	  complex_mul_re_d(1.0/complex_abs_d(&phase0),&phase0);
	  complex_mul_re_d(1.0/complex_abs_d(&phase1),&phase1);
	  
	  // ?
	  phase1.re = cos(2.0*M_PI*cf1*((double)sample_idx)/sample_rate);
	  phase1.im = sin(2.0*M_PI*cf1*((double)sample_idx)/sample_rate);
	  
	  output_idx++;
	  if(output_idx == n_out)
          {
	    result = digital_rf_write_hdf5(drf0, local_index, output0, n_out);
	    result = digital_rf_write_hdf5(drf1, local_index, output1, n_out);
	    local_index+=n_out;
	    
	    for(int j=0; j<n_out ; j++)
	    {
	      output0[j].re=0.0;
	      output0[j].im=0.0;
	      output1[j].re=0.0;
	      output1[j].im=0.0;
	    }
	    output_idx=0;
	    /* old gdf format, replaced with digital rf
	    
	    fprintf(tfile,"data-%06d.gdf %1.20lf\n",file_idx,((double)tv.tv_sec)+(double)tv.tv_usec*1.0/1e6);

	    fflush(tfile);
	    sprintf(fname,"000/data-%06d.gdf",file_idx);
	    f = (FILE *)fopen(fname,"wb");
	    fwrite(output0,sizeof(complex_double),n_out,f);
	    fclose(f);

	    sprintf(fname,"001/data-%06d.gdf",file_idx);
	    f = (FILE *)fopen(fname,"wb");
	    fwrite(output1,sizeof(complex_double),n_out,f);
	    fclose(f);
	    
	    printf(".");
	    fflush(stdout);
	    
	    for(int j=0; j<n_out ; j++)
	    {
	      output0[j].re=0.0;
	      output0[j].im=0.0;
	      output1[j].re=0.0;
	      output1[j].im=0.0;
	    }
	    
	    output_idx=0;
	    file_idx++;
	    // time of leading edge for next sample
	    gettimeofday(&tv, NULL);
	    */
	  }
	}
      }
    }
    int dddc_impl::work(int noutput_items,
			gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items)
    {
      short *in = (short *) input_items[0];
      uint64_t sample_idx0;
      int n_dropped;

      n_dropped=0;

      hid_t dtype;
      
      if (first == 1)
      {
	get_rx_time(noutput_items);

	first = 0;
	//	gettimeofday(&tv, NULL);
	dtype = H5T_NATIVE_DOUBLE;
	char uuid[512] = "THIS_UUID_LACKS_ENTROPY";
	char dirn0[512] = "/data/phasecal/000";
	char dirn1[512] = "/data/phasecal/001";

	//					   file_len,	
	sample_idx0 = t0/win_len;
	printf("sample_idx0 %" PRIu64 "\n",sample_idx0);
	drf0 = digital_rf_create_write_hdf5(dirn0,
					    dtype,
					    ((int)sample_rate)/win_len,
					    3600,
					    sample_idx0,
					    ((int)sample_rate)/win_len,
					    uuid, 0, 0, 1, 1, 1);	

	drf1 = digital_rf_create_write_hdf5(dirn1,
					    dtype,
					    ((int)sample_rate)/win_len,
					    3600,
					    sample_idx0,
					    ((int)sample_rate)/win_len,
					    uuid, 0, 0, 1, 1, 1);

      }
      else 
      {
	n_dropped = detect_overflow(nitems_read(0),nitems_read(0)+noutput_items);
      }
      if(n_dropped > 0)
      {
	consume_samples(in,n_dropped);
      }
      consume_samples(in,noutput_items);
      
      // Tell runtime system how many output items we produced.
      return noutput_items;
    }
  } /* namespace drf */
} /* namespace gr */

