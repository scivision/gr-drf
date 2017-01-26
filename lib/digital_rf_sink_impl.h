/* -*- c++ -*- */
/*
 * Copyright 2015-2016 Juha Vierinen, Ryan Volz.
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

#ifndef INCLUDED_DRF_DIGITAL_RF_SINK_IMPL_H
#define INCLUDED_DRF_DIGITAL_RF_SINK_IMPL_H

#include <drf/digital_rf_sink.h>

extern "C" {
#include <digital_rf.h>
}

namespace gr {
  namespace drf {

    class digital_rf_sink_impl : public digital_rf_sink
    {
     private:
      char d_dir[4096];
      size_t d_sample_size;
      uint64_t d_subdir_cadence_s;
      uint64_t d_file_cadence_ms;
      double d_sample_rate;
      char d_uuid[512];
      bool d_is_complex;
      int d_num_subchannels;
      bool d_stop_on_dropped_packet;

      Digital_rf_write_object *d_drfo;
      hid_t d_dtype;
      uint64_t d_t0s; // start time floored to nearest second in samples from unix epoch
      uint64_t d_t0; // start time in samples from unix epoch
      uint64_t d_local_index;
      uint64_t d_total_dropped;
      bool d_first;

      char *d_zero_buffer;

      // make copy constructor private with no implementation to prevent copying
      digital_rf_sink_impl(const digital_rf_sink_impl& that);

     public:
      digital_rf_sink_impl(char *dir, size_t sample_size,
                           uint64_t subdir_cadence_s, uint64_t file_cadence_ms,
                           double sample_rate, char* uuid, bool is_complex,
                           int num_subchannels, bool stop_on_dropped_packet);
      ~digital_rf_sink_impl();

      void get_rx_time(int n);
      int detect_and_handle_overflow(uint64_t start, uint64_t end, char *in);

      // Where all the action really happens
      int work(int noutput_items,
               gr_vector_const_void_star &input_items,
               gr_vector_void_star &output_items);
    };

  } // namespace drf
} // namespace gr

#endif /* INCLUDED_DRF_DIGITAL_RF_SINK_IMPL_H */

