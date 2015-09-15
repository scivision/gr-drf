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

#ifndef INCLUDED_DRF_DIGITAL_RF_IMPL_H
#define INCLUDED_DRF_DIGITAL_RF_IMPL_H

#include <drf/digital_rf.h>

extern "C" {
#include <digital_rf.h>
}

namespace gr {
  namespace drf {

    class digital_rf_impl : public digital_rf
    {
     private:
      // Nothing to declare in this block.
      Digital_rf_write_object *drf;
      double sample_rate;
      size_t size;
      int files_per_dir;
      int file_len;
      char dirn[4096];
      uint64_t t0, _t0; // start time in unix second 
      uint64_t local_index;
      uint64_t total_dropped;
      double ut0;  // start time in unix seconds
      int first;
      
      char *char_buffer;
      int short_to_char;
      int scale_factor;
      int stop_on_dropped_packet;

     public:
      digital_rf_impl(char *dir, int file_len, int files_per_dir, size_t size, double sample_rate, int short_to_char, int stop_on_dropped_packet);
      ~digital_rf_impl();
      int detect_overflow(uint64_t start, uint64_t end);
      void get_rx_time(int n);
      void enable_short_to_char();
      void short_to_char_conv(short *in, char *out, int len);

      // Where all the action really happens
      int work(int noutput_items,
	       gr_vector_const_void_star &input_items,
	       gr_vector_void_star &output_items);

     // private:
     //  // Nothing to declare in this block.

     // public:
     //  digital_rf_impl(char *dir, int file_len, int files_per_dir, size_t size, double sample_rate, int short_to_char, int stop_on_dropped_packet);
     //  ~digital_rf_impl();

     //  // Where all the action really happens
     //  int work(int noutput_items,
	 //       gr_vector_const_void_star &input_items,
	 //       gr_vector_void_star &output_items);
    };

  } // namespace drf
} // namespace gr

#endif /* INCLUDED_DRF_DIGITAL_RF_IMPL_H */

