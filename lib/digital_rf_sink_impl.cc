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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "digital_rf_sink_impl.h"

extern "C" {
#include <digital_rf.h>
}

#define ZERO_BUFFER_SIZE 10000000

namespace gr {
  namespace drf {

    digital_rf_sink::sptr
    digital_rf_sink::make(char *dir, size_t sample_size,
                          uint64_t subdir_cadence_s, uint64_t file_cadence_ms,
                          double sample_rate, char *uuid, bool is_complex,
                          int num_subchannels, bool stop_on_dropped_packet)
    {
      return gnuradio::get_initial_sptr
        (new digital_rf_sink_impl(dir, sample_size, subdir_cadence_s,
                                  file_cadence_ms, sample_rate, uuid,
                                  is_complex, num_subchannels,
                                  stop_on_dropped_packet));
    }


    /*
     * The private constructor
     */
    digital_rf_sink_impl::digital_rf_sink_impl(
            char *dir, size_t sample_size, uint64_t subdir_cadence_s,
            uint64_t file_cadence_ms, double sample_rate, char* uuid,
            bool is_complex, int num_subchannels, bool stop_on_dropped_packet
    )
      : gr::sync_block("digital_rf_sink",
               gr::io_signature::make(1, 1, sample_size*num_subchannels),
               gr::io_signature::make(0, 0, 0)),
        d_sample_size(sample_size), d_subdir_cadence_s(subdir_cadence_s),
        d_file_cadence_ms(file_cadence_ms), d_sample_rate(sample_rate),
        d_is_complex(is_complex), d_num_subchannels(num_subchannels),
        d_stop_on_dropped_packet(stop_on_dropped_packet)
    {
      char command[4096];
      int i;
      printf("%s\n", dir);
      strcpy(dirn, dir);
      sprintf(command, "mkdir -p %s", dirn);
      printf("%s\n", command);
      fflush(stdout);
      int ignore_this = system(command);
      strcpy(d_uuid, uuid);
      t0 = 1;
      _t0 = 1;
      first = 1;
      printf("subdir_cadence_s %lu file_cadence_ms %lu sample_size %d sample_rate %1.2f\n",
             subdir_cadence_s, file_cadence_ms, (int)sample_size, sample_rate);
      local_index = 0;
      total_dropped = 0;

      zero_buffer = (char *)malloc(ZERO_BUFFER_SIZE*sizeof(char));
      for(i=0; i<ZERO_BUFFER_SIZE; i++)
      {
        zero_buffer[i] = 0;
      }
    }

    /*
     * Our virtual destructor.
     */
    digital_rf_sink_impl::~digital_rf_sink_impl()
    {
      if(!first)
        digital_rf_close_write_hdf5(drf);
      free(zero_buffer);
    }


    int digital_rf_sink_impl::detect_overflow(uint64_t start, uint64_t end)
    {
      std::vector<gr::tag_t> rx_time_tags;
      uint64_t dt;
      int dropped;
      dropped = 0;
      get_tags_in_range(rx_time_tags, 0, start, end, pmt::string_to_symbol("rx_time"));

      //print all tags
      BOOST_FOREACH(const gr::tag_t &rx_time_tag, rx_time_tags)
      {
        const uint64_t offset = rx_time_tag.offset;
        const pmt::pmt_t &value = rx_time_tag.value;

        uint64_t tt0_sec = pmt::to_uint64(pmt::tuple_ref(value, 0));
        double tt0_frac = pmt::to_double(pmt::tuple_ref(value, 1));

        // we should have this many samples
        dt = (((int64_t)d_sample_rate)*tt0_sec + (int64_t)(tt0_frac*d_sample_rate)
              - (int64_t)_t0 - (int64_t)total_dropped);

        dropped = dt  - offset;
        total_dropped += dropped;
        printf("Dropped packet(s). %lu total_dropped %d dropped %u index %d.\n",
               offset, (int)total_dropped, (int)dropped, (int)(offset-start));
      }
      return(dropped);
    }

    void digital_rf_sink_impl::get_rx_time(int n)
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
        t0 = (uint64_t)(d_sample_rate*t0_sec);
        _t0 = ((uint64_t)(((uint64_t)d_sample_rate)*((uint64_t)t0_sec)
               +((uint64_t)(d_sample_rate*t0_frac))));
        printf("offset0 %lu",offset);
      }
    }


    int digital_rf_sink_impl::work(int noutput_items,
                              gr_vector_const_void_star &input_items,
                              gr_vector_void_star &output_items)
    {
      void *in = (void *) input_items[0];
      hid_t dtype;
      int result, i;
      int samples_dropped;

      samples_dropped = 0;

      if(first)
      {
        // sets start time t0
        get_rx_time(noutput_items);

        if(d_is_complex)
        {
          // complex char (int8)
          if(d_sample_size == 2)
          {
            dtype = H5T_NATIVE_CHAR;
          }
          // complex short (int16)
          else if(d_sample_size == 4)
          {
            dtype = H5T_NATIVE_SHORT;
          }
          // complex float (float32)
          else if(d_sample_size == 8)
          {
            dtype = H5T_NATIVE_FLOAT;
          }
          // complex double (float64)
          else if(d_sample_size == 16)
          {
            dtype = H5T_NATIVE_DOUBLE;
          }
          else
          {
            printf("Item size not supported");
            exit(0);
          }
        }
        else
        {
          // char (int8)
          if(d_sample_size == 1)
          {
            dtype = H5T_NATIVE_CHAR;
          }
          // short (int16)
          else if(d_sample_size == 2)
          {
            dtype = H5T_NATIVE_SHORT;
          }
          // float (float32)
          else if(d_sample_size == 4)
          {
            dtype = H5T_NATIVE_FLOAT;
          }
          // double (float64)
          else if(d_sample_size == 8)
          {
            dtype = H5T_NATIVE_DOUBLE;
          }
          else
          {
            printf("Item size not supported");
            exit(0);
          }
        }

        printf("create %s t0 %ld sample_rate %f\n", dirn, t0, d_sample_rate);
        fflush(stdout);
        /*      Digital_rf_write_object * digital_rf_create_write_hdf5(
                    char * directory, hid_t dtype_id, uint64_t subdir_cadence_secs,
                    uint64_t file_cadence_millisecs, uint64_t global_start_sample,
                    double sample_rate, char * uuid_str,
                    int compression_level, int checksum, int is_complex,
                    int num_subchannels, int is_continuous, int marching_dots
                )
        */
        drf = digital_rf_create_write_hdf5(
                dirn, dtype, d_subdir_cadence_s, d_file_cadence_ms, t0,
                d_sample_rate, d_uuid, 0, 0, d_is_complex, d_num_subchannels,
                1, 1);
        printf("done\n");
        first = 0;
      }
      else
      {
        samples_dropped = detect_overflow(nitems_read(0),
                                          nitems_read(0) + noutput_items);
      }
      if(d_stop_on_dropped_packet && samples_dropped > 0)
      {
        printf("Dropped packet. Stopping as requested\n.");
        exit(0);
      }

      result = digital_rf_write_hdf5(drf, local_index, in, noutput_items);
      local_index += noutput_items;

      // if we've dropped packets, write zeros
      if(samples_dropped > 0)
      {
        if(samples_dropped*d_sample_size*d_num_subchannels > ZERO_BUFFER_SIZE)
        {
          printf("Too many dropped samples");
          exit(0);
        }
        result = digital_rf_write_hdf5(drf, local_index, zero_buffer, samples_dropped);
        local_index += samples_dropped;
      }

      if (result){
        printf("nonzero result on write\n");
        exit(-1);
      }

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace drf */
} /* namespace gr */

