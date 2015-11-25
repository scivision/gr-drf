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
#include "digital_rf_impl.h"

extern "C" {
#include <digital_rf.h>
}

namespace gr {
  namespace drf {

    digital_rf::sptr
    digital_rf::make(char *dir, int file_len, int files_per_dir, size_t size, double sample_rate, int short_to_char, int stop_on_dropped_packet)
    {
      return gnuradio::get_initial_sptr
        (new digital_rf_impl(dir, file_len, files_per_dir, size, sample_rate, short_to_char, stop_on_dropped_packet));
    }


    /*
     * The private constructor
     */
    digital_rf_impl::digital_rf_impl(char *d_dir, int d_file_len, int d_files_per_dir, size_t d_size, double d_sample_rate, int s_to_char, int stop_on_dropped_p)
      : gr::sync_block("digital_rf",
		       gr::io_signature::make(1, 1, d_size),
		       gr::io_signature::make(0, 0, 0)),
	sample_rate(d_sample_rate), size(d_size), file_len(d_file_len), files_per_dir(d_files_per_dir)
    {
      char command[4096];
      int i;
      printf("%s\n",d_dir);
      strcpy(dirn,d_dir);
      sprintf(command,"mkdir -p %s",dirn);
      printf("%s\n",command);
      fflush(stdout);
      int ignore_this = system(command);
      first=1;
      printf("file_len %d files_per_dir %d size %d %1.2f\n", file_len, files_per_dir, (int)size, sample_rate);
      local_index=0;
      if(s_to_char == 1){
	short_to_char = 1;
	enable_short_to_char();
      }
      else
      {
	short_to_char=0;
      }
      total_dropped = 0;

      char_buffer = (char *)malloc(10000000*sizeof(char));
      for(i=0 ; i<10000000 ; i++)
      {
	char_buffer[i]=0;
      }
      scale_factor=5;
      stop_on_dropped_packet=stop_on_dropped_p;
    }

    /*
     * Our virtual destructor.
     */
    digital_rf_impl::~digital_rf_impl()
    {
    }

    int digital_rf_impl::detect_overflow(uint64_t start, uint64_t end)
    {
      std::vector<gr::tag_t> rx_time_tags;
      uint64_t dt;
      int dropped;
      dropped=0;
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



    void digital_rf_impl::get_rx_time(int n)
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
        ut0 = ((__float128)t0_sec) + (__float128)t0_frac;
	printf("offset0 %lu",offset);
      }
    }
    
    void digital_rf_impl::short_to_char_conv(short *in, char *out, int len)
    {
      int i;
      if(len > 5000000)
      {
	printf("buffer not large enough!");
	exit(0);
      }
      for(i=0 ; i<len ; i++)
      {
	out[2*i] = (char) (in[2*i]/scale_factor);
	out[2*i+1] = (char) (in[2*i+1]/scale_factor);
      }

    }

    void digital_rf_impl::enable_short_to_char()
    {
      short_to_char = 1;
    }

    int
    digital_rf_impl::work(int noutput_items,
			  gr_vector_const_void_star &input_items,
			  gr_vector_void_star &output_items)
    {
        void *in = (void *) input_items[0];
	hid_t dtype;
	int result,i;
	int samples_dropped;

	samples_dropped=0;

	if(first)
	{
	  get_rx_time(noutput_items);

	  if(size == 2)
	    dtype = H5T_NATIVE_SHORT;
	  else if(size == 8)
	    dtype = H5T_NATIVE_FLOAT;
	  else if(size == 4)
	    if(short_to_char == 1) {
	      printf("8-bit conversion\n");
	      dtype= H5T_NATIVE_CHAR;
	    } else {
	      printf("complex short %d\n",short_to_char);
	      dtype = H5T_NATIVE_SHORT;
	    }

	  printf("create %s t0 %ld %f\n",dirn,t0,sample_rate);
	  fflush(stdout);
	  /*	  Digital_rf_write_object * digital_rf_create_write_hdf5(char * directory, hid_t dtype_id, uint64_t samples_per_file,
								 uint64_t files_per_directory, uint64_t global_start_sample,
								 double sample_rate, char * uuid_str,
								 int compression_level, int checksum, int is_complex,
								 int num_subchannels, int marching_dots);
	  */
	  char uuid[512] = "THIS_UUID_LACKS_ENTROPY";
	  drf = digital_rf_create_write_hdf5(dirn, dtype, file_len, files_per_dir, t0, sample_rate, uuid, 0, 0, 1, 1, 1);
	  printf("done\n");
	  first=0;
	}
	else
	{
	  samples_dropped = detect_overflow(nitems_read(0),nitems_read(0)+noutput_items);
	}
	if(stop_on_dropped_packet == 1 and samples_dropped > 0)
	{
	  printf("Dropped packet. Stopping as requested\n.");
	  exit(0);
	}
	
	// short ints 
	if(size == 2 && short_to_char == 0)
	{
	  result = digital_rf_write_hdf5(drf, local_index, in, noutput_items/2);
	  local_index+=noutput_items/2;
	}
	// complex short ints 
	else if(size == 4 && short_to_char == 0)
	{
	  result = digital_rf_write_hdf5(drf, local_index, in, noutput_items);
	  local_index+=noutput_items;

          // if we've dropped packets, write zeros
          if(samples_dropped > 0)
          {
            if(samples_dropped > 5000000)
            {
              printf("Too many dropped samples");
              exit(0);
            }
            result = digital_rf_write_hdf5(drf, local_index, char_buffer, samples_dropped);
            local_index+=samples_dropped;
          }
	}
	else if(size == 8)
	{
	  result = digital_rf_write_hdf5(drf, local_index, in, noutput_items);
	  local_index+=noutput_items;
	} 
	else if(size == 4 && short_to_char == 1)
	{
	  short_to_char_conv((short *)in, char_buffer, noutput_items);
	  result = digital_rf_write_hdf5(drf, local_index, char_buffer, noutput_items);
	  local_index+=noutput_items;
	  // if we've dropped packets, write zeros
	  if(samples_dropped > 0)
	  {
	    if(samples_dropped > 5000000)
	    {
	      printf("Too many dropped samples");
	      exit(0);
	    }
	    for(i=0;i<2*samples_dropped;i++)
	    {
	      char_buffer[i]=0;
	    }
	    result = digital_rf_write_hdf5(drf, local_index, char_buffer, samples_dropped);
	    local_index+=samples_dropped;
	  }
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

