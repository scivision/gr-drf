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


#ifndef INCLUDED_DRF_DIGITAL_RF_H
#define INCLUDED_DRF_DIGITAL_RF_H

#include <drf/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace drf {

    /*!
     * \brief <+description of block+>
     * \ingroup drf
     *
     */
    class DRF_API digital_rf : virtual public gr::sync_block
    {
     public:
      typedef boost::shared_ptr<digital_rf> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of drf::digital_rf.
       *
       * To avoid accidental use of raw pointers, drf::digital_rf's
       * constructor is in a private implementation
       * class. drf::digital_rf::make is the public interface for
       * creating new instances.
       */
      static sptr make(char *dir, int file_len, int files_per_dir, size_t size, double sample_rate, int short_to_char, int stop_on_dropped_packet);
    };

  } // namespace drf
} // namespace gr

#endif /* INCLUDED_DRF_DIGITAL_RF_H */

