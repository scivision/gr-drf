#!/usr/bin/env python
#
# Record arbitrary number of channels.
#
# (c) 2014 Juha Vierinen
# (c) 2015 Ryan Volz
#
from gnuradio import gr
from gnuradio import uhd
from gnuradio import filter
from gnuradio.filter import firdes

from argparse import ArgumentParser, RawDescriptionHelpFormatter
from textwrap import fill, dedent, TextWrapper
from itertools import cycle, islice, repeat
import sys, time, os, math, re, glob, numpy

import drf, sampler_util

first_file_idx = 0

print 'THOR version 2.0 (The Haystack Observatory Recorder)\n'
print '(c) 2014 Juha Vierinen'
print '(c) 2015 Ryan Volz\n\n'

scriptname = os.path.basename(sys.argv[0])

formatter = RawDescriptionHelpFormatter(scriptname)
width = formatter._width

desc = 'Record data from synchronized USRPs in DigitalRF format.'

epi_pars = [
    '''\
    Multiple mainboards can be specified by repeating the mainboard and
    channel arguments. The subdevice, centerfreq, and gain arguments can also
    be repeated to set those properties per mainboard, but those values will
    be duplicated if necessary to match the number of specified mainboards.
    ''',
    '''\
    Typical usage:
    ''',
]
epi_pars = [fill(dedent(s), width) for s in epi_pars]

egtw = TextWrapper(width=(width-2), break_long_words=False, break_on_hyphens=False,
                   subsequent_indent=' '*(len(scriptname) + 1))
egs = [
    '''\
    {0} -m 192.168.10.2 -c a1,a2 -m 192.168.20.2 -c b1,b2 -d "A:A A:B"
    -f 15e6 -g 0 -r 100e6/24 /data/test
    ''',
    '''\
    {0} -m 192.168.10.2,192.168.10.3 -c a1,a2,b1,b2 -d "A:A A:B"
    -f 15e6 -g 0 -r 100e6/24 /data/test
    ''',
    '''\
    {0} -m 192.168.10.2 -d "A:A A:B" -c ch1,ch2 -f 10e6,20e6
    -m 192.168.20.2 -d "A:A" -c ch3 -f 30e6 -r 1e6 /data/test
    ''',
]
egs = [' \\\n'.join(egtw.wrap(dedent(s.format(scriptname)))) for s in egs]
epi = '\n' + '\n\n'.join(epi_pars + egs) + '\n'

## parse options
parser = ArgumentParser(description=desc, epilog=epi,
                        formatter_class=RawDescriptionHelpFormatter)

parser.add_argument('dir',
                    help='''Data directory, to be filled with channel
                            subdirectories.''')

parser.add_argument('-m', '--mainboard', dest='mboards', action='append',
                    help='''Mainboard address.
                            (default: "192.168.10.x" for x = 2,3,4)''')

parser.add_argument('-d', '--subdevice', dest='subdevs', action='append',
                    help='''USRP subdevice string.
                            (default: "A:A A:B")''')

parser.add_argument('-c', '--channel', dest='chs', action='append',
                    help='''Channel names to use in data directory.
                            (default: "lX,dX" for X = A,B,C)''')

parser.add_argument('-f', '--centerfreq', dest='centerfreqs', action='append',
                    help='''Center frequency.
                            (default: 15e6)''')

parser.add_argument('-g', '--gain', dest='gains', action='append',
                    help='''Gain in dB.
                            (default: 0)''')

parser.add_argument('-a', '--args', dest='stream_args',
                    default='',
                    help='''Common stream args, e.g. "peak=0.125,fullscale=1.0".
                            (default: %(default)s)''')

parser.add_argument('-r', '--samplerate', dest='samplerate',
                    default='1e6',
                    help='''Sample rate in Hz.
                            (default: %(default)s)''')

parser.add_argument('-i', '--dec', dest='dec',
                    default=1, type=int,
                    help='''Integrate and decimate by this factor.
                            (default: %(default)s)''')

parser.add_argument('-t', '--starttime', dest='starttime',
                    type=int,
                    help='''Start time of the experiment in seconds from Unix
                            epoch.
                            (default: %(default)s)''')

parser.add_argument('-p', '--cycle-length', dest='period',
                    default=10, type=int,
                    help='''Repeat time of experiment cycle. Align to start of
                            next cycle if start time has passed.
                            (default: %(default)s)''')

parser.add_argument('-s', '--filesize', dest='filesize',
                    type=int,
                    help='''File size in samples.
                            (default: %(default)s)''')

parser.add_argument('-q', '--stop_on_dropped', dest='stop_on_dropped', action='store_true',
                    help='''Stop on dropped packet.
                            (default: %(default)s)''')

parser.add_argument('--nosync', dest='nosync', action='store_true',
                    help='''No syncing with external clock.
                            (default: %(default)s)''')

op = parser.parse_args()

if op.mboards is None:
    op.mboards = ['192.168.10.2', '192.168.10.3', '192.168.10.4']
if op.subdevs is None:
    op.subdevs = ['A:A A:B']
if op.chs is None:
    op.chs = ['lA,dA', 'lB,dB', 'lC,dC']
if op.centerfreqs is None:
    op.centerfreqs = ['15e6']
if op.gains is None:
    op.gains = ['0']

# separate any combined arguments
# e.g. op.mboards = ['192.168.10.2,192.168.10.3'] becomes ['192.168.10.2', '192.168.10.3']
op.mboards = [a.strip() for arg in op.mboards for a in arg.strip().split(',')]
op.subdevs = [a.strip() for arg in op.subdevs for a in arg.strip().split(',')]
op.chs = [a.strip() for arg in op.chs for a in arg.strip().split(',')]
op.centerfreqs = [float(a.strip()) for arg in op.centerfreqs for a in arg.strip().split(',')]
op.gains = [float(a.strip()) for arg in op.gains for a in arg.strip().split(',')]

# repeat arguments as necessary
nmboards = len(op.mboards)
nchs = len(op.chs)
op.subdevs = list(islice(cycle(op.subdevs), 0, nmboards))
op.centerfreqs = list(islice(cycle(op.centerfreqs), 0, nchs))
op.gains = list(islice(cycle(op.gains), 0, nchs))

# evaluate samplerate to float
op.samplerate = eval(op.samplerate)

print 'Main boards: ',op.mboards
print 'Subdevices: ',op.subdevs
print 'Channel names: ',op.chs
print 'Frequency: ',op.centerfreqs
print 'Gain: ',op.gains
print 'Stream arguments: ',op.stream_args
print 'Sample rate: ',op.samplerate
print 'Data dir: ',op.dir

# sanity check, number of total subdevs should be same as number of channels
mboards_bychan = []
subdevs_bychan = []
for mb, sd in zip(op.mboards, op.subdevs):
    sds = sd.split()
    mbs = list(repeat(mb, len(sds)))
    mboards_bychan.extend(mbs)
    subdevs_bychan.extend(sds)
if len(subdevs_bychan) != nchs:
    raise ValueError('Number of device channels does not match the number of channel names provided')

r = gr.enable_realtime_scheduling()

if r == gr.RT_OK:
   print('Realtime scheduling enabled')
else:
   print 'Note: failed to enable realtime scheduling'

# create usrp source block
mboard_addrstr = ','.join(['addr{0}={1}'.format(n, s.strip()) for n, s in enumerate(op.mboards)])
if op.dec > 1:
    cpu_format = 'fc32'
else:
    cpu_format = 'sc16'
u = uhd.usrp_source(
   device_addr='%s,recv_buff_size=100000000'%(mboard_addrstr),
   stream_args=uhd.stream_args(
      cpu_format=cpu_format,
      otw_format='sc16',
      channels=range(nchs),
      args=op.stream_args))

if not op.nosync:
   u.set_clock_source('external', uhd.ALL_MBOARDS)
   u.set_time_source('external', uhd.ALL_MBOARDS)

if op.filesize == None:
   op.filesize=op.samplerate/op.dec

# wait until time 0.2 to 0.5 past full second, then latch.
# we have to trust NTP to be 0.2 s accurate. It might be a good idea to do a ntpdate before running
# uhdsampler.py
tt = time.time()
while tt-math.floor(tt) < 0.2 or tt-math.floor(tt) > 0.3:
    tt = time.time()
    time.sleep(0.01)
print('Latching at '+str(tt))
if not op.nosync:
   u.set_time_unknown_pps(uhd.time_spec(math.ceil(tt)+1.0))

for mb_num in range(nmboards):
    u.set_subdev_spec(op.subdevs[mb_num], mb_num)
u.set_samp_rate(op.samplerate)
op.samplerate = u.get_samp_rate() # may be different than desired
for ch_num in range(nchs):
    u.set_center_freq(op.centerfreqs[ch_num], ch_num)
    u.set_gain(op.gains[ch_num], ch_num)

if op.stop_on_dropped == True:
   op.stop_on_dropped = 1
else:
   op.stop_on_dropped = 0

# find next suitable launch time
if op.starttime is None:
    op.starttime = math.ceil(time.time())

print 'Launch time: ',op.starttime
op.starttime = sampler_util.find_next(op.starttime,op.period)
print 'Starting time: ',op.starttime
if not op.nosync:
   u.set_start_time(uhd.time_spec(op.starttime))

# create flowgraph
fg = gr.top_block()

dirs = [os.path.join(op.dir, ch) for ch in op.chs]
if op.dec > 1:
    taps = firdes.low_pass_2(1.0,
                             float(op.samplerate),
                             float(op.samplerate)/float(op.dec)/2.0,
                             0.2*(float(op.samplerate)/float(op.dec)),
                             80.0,
                             window=firdes.WIN_BLACKMAN_hARRIS)

    lpfs = [filter.freq_xlating_fir_filter_ccf(op.dec,taps,0.0,op.samplerate) for k in range(nchs)]
    dsts = [drf.digital_rf(
                d, int(op.filesize), int(3600),
                gr.sizeof_gr_complex, op.samplerate/op.dec, 0, op.stop_on_dropped,
            ) for d in dirs]

    for k in range(nchs):
        fg.connect((u, k), (lpfs[k], 0), (dsts[k], 0))

        sampler_util.write_metadata_drf(
            dirs[k], 1, [op.centerfreqs[k]], op.starttime,
            dtype='<f4', itemsize=gr.sizeof_gr_complex, sr=op.samplerate/op.dec,
            extra_keys=['usrp_ip', 'usrp_subdev', 'usrp_gain', 'usrp_stream_args'],
            extra_values=[mboards_bychan[k], subdevs_bychan[k],
                          op.gains[k], op.stream_args],
        )
else:
    dsts = [drf.digital_rf(
                d, int(op.filesize), int(3600),
                2*gr.sizeof_short, op.samplerate, 0, op.stop_on_dropped,
            ) for d in dirs]

    for k in range(nchs):
        fg.connect((u, k), (dsts[k], 0))

        sampler_util.write_metadata_drf(
            dirs[k], 1, [op.centerfreqs[k]], op.starttime,
            dtype='<i2', itemsize=2*gr.sizeof_short, sr=op.samplerate,
            extra_keys=['usrp_ip', 'usrp_subdev', 'usrp_gain', 'usrp_stream_args'],
            extra_values=[mboards_bychan[k], subdevs_bychan[k],
                          op.gains[k], op.stream_args],
        )

fg.run()
