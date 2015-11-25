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

#ifndef INCLUDED_DRF_DDDC_IMPL_H
#define INCLUDED_DRF_DDDC_IMPL_H

extern "C" {
#include <digital_rf.h>
}

#define JUHA_DDDC_NDC_0 25000000
#define JUHA_DDDC_NDC_1 250000000


typedef struct complex_double_str 
{
  double re;
  double im;
} complex_double;

void complex_add_d(complex_double *a, complex_double *res);
void complex_mul_d(complex_double *a, complex_double *res);
void complex_mul_re_d(double *a, complex_double *res);

#include <drf/dddc.h>

namespace gr {
  namespace drf {

    class dddc_impl : public dddc
    {
     private:

      Digital_rf_write_object *drf0;
      Digital_rf_write_object *drf1;
      // Nothing to declare in this block.
      int win_idx;
      int win_len;
      complex_double *output0;
      complex_double *output1;
      int output_idx;
      int n_out;
      int file_idx;
      
      complex_double *dsin0;
      complex_double *dsin1;
      uint64_t sample_idx;
      uint64_t t0;
      uint64_t _t0;
      complex_double phase0;
      complex_double phase1;
      complex_double phase0_rot;
      complex_double phase1_rot;
      complex_double comp0;
      complex_double comp1;
      double dc0;
      double dc1;
      int n_dc;
      double sample_rate;
      double cf0;
      double cf1;
      int first;

      uint64_t total_dropped;
      uint64_t local_index;
      
      FILE *tfile;

      int detect_overflow(uint64_t start, uint64_t end);
      void get_rx_time(int n);
      void consume_samples(short *in, int noutput_samples);

     public:
      dddc_impl(char *filter_file, int len, double f0, double f1, int n, double sr);
      ~dddc_impl();

      // Where all the action really happens
      int work(int noutput_items,
	       gr_vector_const_void_star &input_items,
	       gr_vector_void_star &output_items);

    };

  } // namespace drf
} // namespace gr

#endif /* INCLUDED_DRF_DDDC_IMPL_H */

