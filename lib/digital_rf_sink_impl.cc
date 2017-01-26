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

#include <stdexcept>
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

      if(d_is_complex)
      {
        // complex char (int8)
        if(d_sample_size == 2) {
          d_dtype = H5T_NATIVE_CHAR;
        }
        // complex short (int16)
        else if(d_sample_size == 4) {
          d_dtype = H5T_NATIVE_SHORT;
        }
        // complex float (float32)
        else if(d_sample_size == 8) {
          d_dtype = H5T_NATIVE_FLOAT;
        }
        // complex double (float64)
        else if(d_sample_size == 16) {
          d_dtype = H5T_NATIVE_DOUBLE;
        }
        else {
          std::invalid_argument("Item size not supported");
        }
      }
      else
      {
        // char (int8)
        if(d_sample_size == 1) {
          d_dtype = H5T_NATIVE_CHAR;
        }
        // short (int16)
        else if(d_sample_size == 2) {
          d_dtype = H5T_NATIVE_SHORT;
        }
        // float (float32)
        else if(d_sample_size == 4) {
          d_dtype = H5T_NATIVE_FLOAT;
        }
        // double (float64)
        else if(d_sample_size == 8) {
          d_dtype = H5T_NATIVE_DOUBLE;
        }
        else {
          std::invalid_argument("Item size not supported");
        }
      }

      strcpy(d_dir, dir);
      sprintf(command, "mkdir -p %s", d_dir);
      printf("%s\n", command);
      fflush(stdout);
      int ignore_this = system(command);

      strcpy(d_uuid, uuid);

      printf("subdir_cadence_s %lu file_cadence_ms %lu sample_size %d rate %1.2f\n",
             subdir_cadence_s, file_cadence_ms, (int)sample_size, sample_rate);

      d_zero_buffer = (char *)malloc(ZERO_BUFFER_SIZE*sizeof(char));
      for(i=0; i<ZERO_BUFFER_SIZE; i++) {
        d_zero_buffer[i] = 0;
      }

      d_first = 1;
      d_t0s = 1;
      d_t0 = 1;
      d_local_index = 0;
      d_total_dropped = 0;
    }

    /*
     * Our virtual destructor.
     */
    digital_rf_sink_impl::~digital_rf_sink_impl()
    {
      digital_rf_close_write_hdf5(d_drfo);
      free(d_zero_buffer);
    }

    bool
    digital_rf_sink_impl::start()
    {
      // set state to start a new writer instance
      d_first = 1;
      // update start sample index based on previous written (if any from stop)
      // just in case there are no new time tags (implying continuous data)
      d_t0s += d_local_index + d_total_dropped;
      d_t0 += d_local_index + d_total_dropped;
      d_local_index = 0;
      d_total_dropped = 0;
      return true;
    }

    bool
    digital_rf_sink_impl::stop()
    {
      // close existing writer instance
      digital_rf_close_write_hdf5(d_drfo);
      return true;
    }


    void
    digital_rf_sink_impl::get_rx_time(int n)
    {
      struct timeval tv;

      std::vector<gr::tag_t> rx_time_tags;
      get_tags_in_range(rx_time_tags, 0, 0, n, pmt::string_to_symbol("rx_time"));

      double t0_frac;
      uint64_t t0_sec;

      //print all tags
      BOOST_FOREACH(const gr::tag_t &rx_time_tag, rx_time_tags) {
        const uint64_t offset = rx_time_tag.offset;
        const pmt::pmt_t &value = rx_time_tag.value;

        t0_sec = pmt::to_uint64(pmt::tuple_ref(value, 0));
        t0_frac = pmt::to_double(pmt::tuple_ref(value, 1));
        d_t0s = (uint64_t)(d_sample_rate*t0_sec);
        d_t0 = ((uint64_t)(((uint64_t)d_sample_rate)*((uint64_t)t0_sec)
               +((uint64_t)(d_sample_rate*t0_frac))));
        printf("Time tag @ %lu, %ld\n", offset, d_t0s);
      }
    }

    int
    digital_rf_sink_impl::detect_and_handle_overflow(uint64_t start,
                                                     uint64_t end,
                                                     char *in)
    {
      std::vector<gr::tag_t> rx_time_tags;
      uint64_t dt;
      int dropped = 0;
      int consumed = 0;
      int filled;
      int result;

      get_tags_in_range(rx_time_tags, 0, start, end, pmt::string_to_symbol("rx_time"));

      //print all tags
      BOOST_FOREACH(const gr::tag_t &rx_time_tag, rx_time_tags) {
        const uint64_t offset = rx_time_tag.offset;
        const pmt::pmt_t &value = rx_time_tag.value;

        uint64_t tt0_sec = pmt::to_uint64(pmt::tuple_ref(value, 0));
        double tt0_frac = pmt::to_double(pmt::tuple_ref(value, 1));

        // we should have this many samples
        dt = (((int64_t)d_sample_rate)*tt0_sec + (int64_t)(tt0_frac*d_sample_rate)
              - (int64_t)d_t0 - (int64_t)d_total_dropped);

        dropped = dt - offset;
        d_total_dropped += dropped;
        printf("\nDropped %u packet(s) @ %lu, total_dropped %d\n",
               (int)dropped, offset + d_total_dropped - dropped,
               (int)d_total_dropped);

        // write in-sequence data up to offset
        result = digital_rf_write_hdf5(d_drfo, d_local_index,
                                       in + consumed*d_sample_size*d_num_subchannels,
                                       offset - d_local_index);
        if(result) {
          throw std::runtime_error("Nonzero result on write");
        }
        consumed += offset - d_local_index;
        d_local_index = offset;

        if(d_stop_on_dropped_packet && dropped > 0) {
          printf("Stopping as requested\n");
          return WORK_DONE;
        }

        // if we've dropped packets, write zeros
        while(dropped > 0) {
          if(dropped*d_sample_size*d_num_subchannels <= ZERO_BUFFER_SIZE) {
            filled = dropped;
          }
          else {
            filled = ZERO_BUFFER_SIZE/d_sample_size/d_num_subchannels;
          }
          result = digital_rf_write_hdf5(d_drfo, d_local_index, d_zero_buffer, filled);
          if(result) {
            throw std::runtime_error("Nonzero result on write");
          }
          d_local_index += filled;
          dropped -= filled;
        }
      }
      return(consumed);
    }


    int
    digital_rf_sink_impl::work(int noutput_items,
                               gr_vector_const_void_star &input_items,
                               gr_vector_void_star &output_items)
    {
      char *in = (char *)input_items[0];
      int result, i;
      int samples_consumed = 0;

      if(d_first) {
        // sets start time d_t0s
        get_rx_time(noutput_items);

        printf("Creating %s t0 %ld\n", d_dir, d_t0s);
        fflush(stdout);
        /*      Digital_rf_write_object * digital_rf_create_write_hdf5(
                    char * directory, hid_t dtype_id, uint64_t subdir_cadence_secs,
                    uint64_t file_cadence_millisecs, uint64_t global_start_sample,
                    double sample_rate, char * uuid_str,
                    int compression_level, int checksum, int is_complex,
                    int num_subchannels, int is_continuous, int marching_dots
                )
        */
        d_drfo = digital_rf_create_write_hdf5(
                d_dir, d_dtype, d_subdir_cadence_s, d_file_cadence_ms, d_t0s,
                d_sample_rate, d_uuid, 0, 0, d_is_complex, d_num_subchannels,
                1, 1);
        if(!d_drfo) {
          throw std::runtime_error("Failed to create Digital RF writer object");
        }
        printf("done\n");
        d_first = 0;
      }
      else {
        samples_consumed = detect_and_handle_overflow(nitems_read(0),
                                                      nitems_read(0) + noutput_items,
                                                      in);
      }

      in += samples_consumed*d_sample_size*d_num_subchannels;
      result = digital_rf_write_hdf5(d_drfo, d_local_index, in,
                                     noutput_items - samples_consumed);
      if(result) {
        throw std::runtime_error("Nonzero result on write");
      }
      d_local_index += noutput_items;

      // Tell runtime system how many output items we produced.
      return noutput_items;
    }

  } /* namespace drf */
} /* namespace gr */

