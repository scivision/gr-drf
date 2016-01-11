#!/usr/bin/env python
#
# Record plasma line and ion line channels to ring buffer on disk.
#
# (c) 2014 Juha Vierinen
#
from gnuradio import eng_notation
from gnuradio import gr
from gnuradio import uhd
from gnuradio.eng_option import eng_option
from gnuradio import filter
from gnuradio.filter import firdes
import gnuradio.blocks

from optparse import OptionParser
import sys, time, os, math, re, glob, numpy

import drf, sampler_util

first_file_idx = 0

print "THOR version 2.0 (The Haystack Observatory Recorder)\n"
print "(c) 2014 Juha Vierinen\n\n"

## parse options
parser = OptionParser(option_class=eng_option)

parser.add_option("-m", "--mainboard", dest="mboard", type="string", default="192.168.30.2",
                  help="Mainboard addresses. (default %default)")

parser.add_option("-c", "--centerfreqs", dest="centerfreqs", action="store", type="string", default="126.2e6,10e6",
                  help="Center frequencies (default %default)")

parser.add_option("-g", "--gain", dest="gain", action="store", default=0,
                  help="Gain (default %default)")

parser.add_option("-p", "--cycle-length", dest="period", action="store",
                  type="int", default=10,
                  help="Repeat time of experiment cycle. Align to start of " +
                       "next cycle if start time has passed (default %default)")

parser.add_option("-f", "--filesize", dest="filesize", action="store",type="int",
                  help="File size (samples)")

parser.add_option("-r", "--samplerate", dest="samplerate", action="store", type="long", default=1000000,
                  help="Sample rate (default %default Hz)")

parser.add_option("-i", "--dec", dest="dec", type="int", action="store", default=1,
                  help="Integrate and decimate by this factor (default = %default)")

parser.add_option("-s", "--starttime", dest="starttime", action="store",
                  type="int",
                  help="Start time of the experiment (unix time)")

parser.add_option("-d", "--subdevice", dest="subdev", action="store",
                  type="string", default="A:A A:B",
                  help="USRP subdevice string (default %default).")

parser.add_option("-0", "--dir0", dest="dir0", action="store", default=None,
                  type="string",
                  help="Prefix for directory 0.")

parser.add_option("-1", "--dir1", dest="dir1", action="store", default=None,
                  type="string",
                  help="Prefix for directory 1.")

parser.add_option("-q", "--stop_on_dropped", dest="stop_on_dropped", action="store_true",
                  help="Stop on dropped packet.")
parser.add_option("-n", "--nosync", dest="nosync", action="store_true",
                  help="No synching with external clock.")

(op, args) = parser.parse_args()

op.centerfreqs = numpy.array(op.centerfreqs.strip().split(","),dtype=numpy.float64)
print(op.centerfreqs)

r = gr.enable_realtime_scheduling()

if r == gr.RT_OK:
   print("Realtime scheduling enabled")
else:
   print "Note: failed to enable realtime scheduling"

# create usrp source block
u = uhd.usrp_source(
   device_addr="addr=%s,recv_buff_size=100000000"%(op.mboard),
   stream_args=uhd.stream_args(
      cpu_format="sc16",
      otw_format="sc16",
      channels=range(2)))

if not op.nosync:
   u.set_clock_source("external", uhd.ALL_MBOARDS)
   u.set_time_source("external", uhd.ALL_MBOARDS)
#u.set_clock_source("none", uhd.ALL_MBOARDS)
#u.set_time_source("none", uhd.ALL_MBOARDS)

if op.dec > 1:
    taps = firdes.low_pass_2(1.0,
                             float(op.samplerate),
                             float(op.samplerate)/float(op.dec)/2.0,
                             0.2*(float(op.samplerate)/float(op.dec)),
                       	     80.0,
                             window=firdes.WIN_BLACKMAN_hARRIS)

    lpf0 = filter.freq_xlating_fir_filter_scf(op.dec,taps,0.0,op.samplerate)
    lpf1 = filter.freq_xlating_fir_filter_scf(op.dec,taps,0.0,op.samplerate)

if op.filesize == None:
   op.filesize=op.samplerate/op.dec

# wait until time 0.2 to 0.5 past full second, then latch.
# we have to trust NTP to be 0.2 s accurate. It might be a good idea to do a ntpdate before running
# uhdsampler.py
tt = time.time()
while tt-math.floor(tt) < 0.2 or tt-math.floor(tt) > 0.3:
    tt = time.time()
    time.sleep(0.01)
print("Latching at "+str(tt))
if not op.nosync:
   u.set_time_unknown_pps(uhd.time_spec(math.ceil(tt)+1.0))

u.set_subdev_spec(op.subdev)
u.set_samp_rate(op.samplerate)
u.set_center_freq(op.centerfreqs[0],0)
u.set_center_freq(op.centerfreqs[1],1)
u.set_gain(op.gain,0)
u.set_gain(op.gain,1)

# create flowgraph
fg = gr.top_block()

dst_0 = None
dst_1 = None


if op.stop_on_dropped == True:
   op.stop_on_dropped = 1
else:
   op.stop_on_dropped = 0

if op.dec > 1:
    dst_0 = drf.digital_rf(op.dir0, int(op.filesize), int(3600), gr.sizeof_gr_complex, op.samplerate,0, op.stop_on_dropped)
    dst_1 = drf.digital_rf(op.dir1, int(op.filesize), int(3600), gr.sizeof_gr_complex, op.samplerate,0, op.stop_on_dropped)
else:
    dst_0 = drf.digital_rf(op.dir0, int(op.filesize), int(3600), 2*gr.sizeof_short, op.samplerate,0, op.stop_on_dropped)
    dst_1 = drf.digital_rf(op.dir1, int(op.filesize), int(3600), 2*gr.sizeof_short, op.samplerate,0, op.stop_on_dropped)

# find next suitable launch time
if op.starttime is None:
    op.starttime = math.ceil(time.time())
    b=time.strftime("%Y.%m.%d_%H.%M.%S",time.strptime(time.ctime(op.starttime)))

op.starttime = sampler_util.find_next(op.starttime,op.period)
if not op.nosync:
   u.set_start_time(uhd.time_spec(op.starttime))

if op.dec > 1:
    fg.connect((u, 0), (lpf0,0), (dst_0,0))
    fg.connect((u, 1), (lpf1,0), (dst_1,0))
else:
    fg.connect((u, 0), (dst_0,0))
    fg.connect((u, 1), (dst_1,0))

print "Launch time: ",op.starttime
print "Sample rate: ",op.samplerate
print "Main board: ",op.mboard
print "Frequencies: ",op.centerfreqs
print "Gains: ",op.gain
print "Starting time: ",op.starttime
print "Dir 0: ",op.dir0
print "Dir 1: ",op.dir1

if op.dec > 1:
    sampler_util.write_metadata_drf(op.dir0,1,[op.centerfreqs[0]],op.starttime,dtype="<f4",itemsize=4,sr=op.samplerate/op.dec,extra_keys=["usrp_ip"],extra_values=[op.mboard])
    sampler_util.write_metadata_drf(op.dir1,1,[op.centerfreqs[1]],op.starttime,dtype="<f4",itemsize=4,sr=op.samplerate/op.dec,extra_keys=["usrp_ip"],extra_values=[op.mboard])
else:
    sampler_util.write_metadata_drf(op.dir0,1,[op.centerfreqs[0]],op.starttime,dtype="<i2",itemsize=4,sr=op.samplerate,extra_keys=["usrp_ip"],extra_values=[op.mboard])
    sampler_util.write_metadata_drf(op.dir1,1,[op.centerfreqs[1]],op.starttime,dtype="<i2",itemsize=4,sr=op.samplerate,extra_keys=["usrp_ip"],extra_values=[op.mboard])

fg.start()

while(True):
   time.sleep(1)
