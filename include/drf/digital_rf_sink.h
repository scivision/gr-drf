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


#ifndef INCLUDED_DRF_DIGITAL_RF_SINK_H
#define INCLUDED_DRF_DIGITAL_RF_SINK_H

#include <drf/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace drf {

    /*!
     * \brief Write data in Digital RF format.
     * \ingroup drf
     *
     */
    class DRF_API digital_rf_sink : virtual public gr::sync_block
    {
     public:
      typedef boost::shared_ptr<digital_rf_sink> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of drf::digital_rf_sink.
       *
       * \param dir Directory to write to.
       * \param sample_size Size of the input data items.
       * \param subdir_cadence_s Number of seconds of data per subdirectory.
       * \param file_cadence_ms Number of milliseconds of data per file.
       * \param sample_rate Sample rate in Hz.
       * \param uuid Unique ID to associate with this data, for pairing metadata.
       * \param is_complex True if the data samples are complex.
       * \param num_subchannels Number of subchannels (i.e. vector length).
       * \param stop_on_dropped_packet If True, stop when a packet is dropped.
       *
       * To avoid accidental use of raw pointers, drf::digital_rf_sink's
       * constructor is in a private implementation
       * class. drf::digital_rf_sink::make is the public interface for
       * creating new instances.
       */
      static sptr make(char *dir, size_t sample_size,
                       uint64_t subdir_cadence_s, uint64_t file_cadence_ms,
                       double sample_rate, char* uuid, bool is_complex,
                       int num_subchannels, bool stop_on_dropped_packet);
    };

  } // namespace drf
} // namespace gr

#endif /* INCLUDED_DRF_DIGITAL_RF_SINK_H */

