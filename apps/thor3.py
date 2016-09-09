#!/usr/bin/env python
#
# Record data from synchronized USRPs in DigitalRF format.
#
# (c) 2014 Juha Vierinen
# (c) 2015-2016 Ryan Volz
#
from __future__ import print_function

import sys
import os
import math
import re
import time
import datetime
import dateutil.parser
import pytz
import numpy as np
from argparse import ArgumentParser, RawDescriptionHelpFormatter
from textwrap import fill, dedent, TextWrapper
from itertools import chain, cycle, islice, repeat
from ast import literal_eval
from subprocess import call
from gnuradio import gr
from gnuradio import uhd
from gnuradio import filter
from gnuradio.filter import firdes

import drf
import digital_metadata as dmd

first_file_idx = 0

print('THOR version 3.0 (The Haystack Observatory Recorder)\n')
print('(c) 2014 Juha Vierinen')
print('(c) 2015-2016 Ryan Volz\n\n')

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
                            (default: first device found)''')

parser.add_argument('-d', '--subdevice', dest='subdevs', action='append',
                    help='''USRP subdevice string.
                            (default: "A:A")''')

parser.add_argument('-c', '--channel', dest='chs', action='append',
                    help='''Channel names to use in data directory.
                            (default: "ch0")''')

parser.add_argument('-f', '--centerfreq', dest='centerfreqs', action='append',
                    help='''Center frequency.
                            (default: 15e6)''')

parser.add_argument('-g', '--gain', dest='gains', action='append',
                    help='''Gain in dB.
                            (default: 0)''')

parser.add_argument('--devargs', dest='dev_args', action='append',
                    default=['recv_buff_size=100000000'],
                    help='''Device arguments, e.g. "master_clock_rate=30e6".
                            (default: %(default)s)''')

parser.add_argument('-a', '--streamargs', dest='stream_args', action='append',
                    help='''Stream arguments, e.g. "peak=0.125,fullscale=1.0".
                            (default: '')''')

parser.add_argument('-r', '--samplerate', dest='samplerate',
                    default='1e6',
                    help='''Sample rate in Hz.
                            (default: %(default)s)''')

parser.add_argument('-i', '--dec', dest='dec',
                    default=1, type=int,
                    help='''Integrate and decimate by this factor.
                            (default: %(default)s)''')

parser.add_argument('-s', '--starttime', dest='starttime',
                    help='''Start time of the experiment in ISO8601 format:
                            2016-01-01T15:24:00Z
                            (default: %(default)s)''')

parser.add_argument('-e', '--endtime', dest='endtime',
                    help='''End time of the experiment in ISO8601 format:
                            2016-01-01T16:24:00Z
                            (default: %(default)s)''')

parser.add_argument('-p', '--cycle-length', dest='period',
                    default=10, type=int,
                    help='''Repeat time of experiment cycle. Align to start of
                            next cycle if start time has passed.
                            (default: %(default)s)''')

parser.add_argument('-n', '--file_cadence_ms', dest='file_cadence_ms',
                    default=1000, type=int,
                    help='''Number of milliseconds of data per file.
                            (default: %(default)s)''')

parser.add_argument('--subdir_cadence_s', dest='subdir_cadence_s',
                    default=3600, type=int,
                    help='''Number of seconds of data per subdirectory.
                            (default: %(default)s)''')

parser.add_argument('--metadata', action='append', metavar='{KEY}={VALUE}',
                    help='''Key, value metadata pairs to include with data.
                            (default: "")''')

parser.add_argument('--stop_on_dropped', dest='stop_on_dropped', action='store_true',
                    help='''Stop on dropped packet.
                            (default: %(default)s)''')

parser.add_argument('--nosync', dest='nosync', action='store_true',
                    help='''No syncing with external clock.
                            (default: %(default)s)''')

parser.add_argument('--realtime', dest='realtime', action='store_true',
                    help='''Enable realtime scheduling if possible.
                            (default: %(default)s)''')

op = parser.parse_args()

if op.mboards is None:
    # use empty string for unspecified motherboard because we want len(op.mboards)==1
    op.mboards = ['']
if op.subdevs is None:
    op.subdevs = ['A:A']
if op.chs is None:
    op.chs = ['ch0']
if op.centerfreqs is None:
    op.centerfreqs = ['15e6']
if op.gains is None:
    op.gains = ['0']
if op.dev_args is None:
    op.dev_args = []
if op.stream_args is None:
    op.stream_args = []
if op.metadata is None:
    op.metadata = []

# separate any combined arguments
# e.g. op.mboards = ['192.168.10.2,192.168.10.3'] becomes ['192.168.10.2', '192.168.10.3']
op.mboards = [a.strip() for arg in op.mboards for a in arg.strip().split(',')]
op.subdevs = [a.strip() for arg in op.subdevs for a in arg.strip().split(',')]
op.chs = [a.strip() for arg in op.chs for a in arg.strip().split(',')]
op.centerfreqs = [float(a.strip()) for arg in op.centerfreqs for a in arg.strip().split(',')]
op.gains = [float(a.strip()) for arg in op.gains for a in arg.strip().split(',')]
op.dev_args = [a.strip() for arg in op.dev_args for a in arg.strip().split(',')]
op.stream_args = [a.strip() for arg in op.stream_args for a in arg.strip().split(',')]
op.metadata = [a.strip() for arg in op.metadata for a in arg.strip().split(',')]

# repeat arguments as necessary
nmboards = len(op.mboards)
nchs = len(op.chs)
op.subdevs = list(islice(cycle(op.subdevs), 0, nmboards))
op.centerfreqs = list(islice(cycle(op.centerfreqs), 0, nchs))
op.gains = list(islice(cycle(op.gains), 0, nchs))

# evaluate samplerate to float
op.samplerate = float(eval(op.samplerate))

# create device_addr string to identify the requested device(s)
mboard_strs = []
for n, mb in enumerate(op.mboards):
    if not mb:
        break
    elif re.match(r'.+=.+', mb):
        idtype, mb = mb.split('=')
    elif re.match(r'[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}', mb):
        idtype = 'addr'
    elif re.match(r'[0-9]{1,}', mb):
        idtype = 'serial'
    elif re.match(r'usrp[123]', mb) or re.match(r'b2[01]0', mb) or re.match(r'x3[01]0', mb):
        idtype = 'type'
    else:
        idtype = 'name'
    s = '{idtype}{n}={mb}'.format(idtype=idtype, n=n, mb=mb.strip())
    mboard_strs.append(s)

# convert metadata strings to a dictionary
metadata_dict = {}
for a in op.metadata:
    try:
        k, v = a.split('=')
    except ValueError:
        k = None
        v = a
    try:
        v = literal_eval(v)
    except ValueError:
        pass
    if k is None:
        metadata_dict.setdefault('metadata', []).append(v)
    else:
        metadata_dict[k] = v

print('Main boards: ',mboard_strs)
print('Subdevices: ',op.subdevs)
print('Channel names: ',op.chs)
print('Frequency: ',op.centerfreqs)
print('Gain: ',op.gains)
print('Device arguments: ',op.dev_args)
print('Stream arguments: ',op.stream_args)
print('Sample rate: ',op.samplerate)
print('Data dir: ',op.dir)
print('Metadata: ',metadata_dict)

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

if op.realtime:
    r = gr.enable_realtime_scheduling()

    if r == gr.RT_OK:
       print('Realtime scheduling enabled')
    else:
       print('Note: failed to enable realtime scheduling')

# create usrp source block
if op.dec > 1:
    cpu_format = 'fc32'
else:
    cpu_format = 'sc16'
u = uhd.usrp_source(
    device_addr=','.join(chain(mboard_strs, op.dev_args)),
    stream_args=uhd.stream_args(
        cpu_format=cpu_format,
        otw_format='sc16',
        channels=range(nchs),
        args=','.join(op.stream_args)
    ),
)

if not op.nosync:
    # try sync sources and test by waiting for pps
    for source in ['external', 'gpsdo']:
        try:
            u.set_clock_source(source, uhd.ALL_MBOARDS)
            u.set_time_source(source, uhd.ALL_MBOARDS)
        except RuntimeError:
            continue

        t0 = u.get_time_last_pps(0).to_ticks(1)
        time.sleep(1)
        if u.get_time_last_pps(0).to_ticks(1) != t0:
            synced = True
            print('Using {0} ref/PPS for synchronization.'.format(source))
            break
    else:
        raise RuntimeError('No PPS signal detected. Run with --nosync?')

for mb_num in range(nmboards):
    u.set_subdev_spec(op.subdevs[mb_num], mb_num)
u.set_samp_rate(op.samplerate)
op.samplerate = u.get_samp_rate() # may be different than desired
for ch_num in range(nchs):
    u.set_center_freq(op.centerfreqs[ch_num], ch_num)
    u.set_gain(op.gains[ch_num], ch_num)

# print current time and NTP status
call(('timedatectl', 'status'))

# parse time arguments as very last thing before launching
if op.starttime is None:
    st0 = int(math.ceil(time.time())) + 3
else:
    dtst0 = dateutil.parser.parse(op.starttime)
    st0 = int((dtst0 - datetime.datetime(1970,1,1,tzinfo=pytz.utc)).total_seconds())

    print('Start time: %s (%d)' % (dtst0.strftime('%a %b %d %H:%M:%S %Y'), st0))

if op.endtime is None:
    et0 = None
else:
    dtet0 = dateutil.parser.parse(op.endtime)
    et0 = int((dtet0 - datetime.datetime(1970,1,1,tzinfo=pytz.utc)).total_seconds())

    print('End time: %s (%d)' % (dtet0.strftime('%a %b %d %H:%M:%S %Y'), et0))

# find next suitable launch time
soon = int(math.ceil(time.time()))
periods_until_next = (max(soon - st0, 0) - 1)//op.period + 1
st = st0 + periods_until_next*op.period
dtst = datetime.datetime.utcfromtimestamp(st)
print('Launch time: %s (%d)' % (dtst.strftime('%a %b %d %H:%M:%S %Y'), st))
u.set_start_time(uhd.time_spec(st))

if et0 is not None and st >= et0:
    raise ValueError('End time is before launch time!')

# create data directory so ringbuffer code can be started while waiting to launch
if not os.path.isdir(op.dir):
    os.makedirs(op.dir)

# wait for the start time if it is not past
while (st - time.time()) > 10:
    print('Standby %d s remaining...' % (st - time.time()))
    sys.stdout.flush()
    time.sleep(1)

# wait until time 0.2 to 0.5 past full second, then latch.
# we have to trust NTP to be 0.2 s accurate. It might be a good idea to do a ntpdate before running
# uhdsampler.py
tt = time.time()
while tt-math.floor(tt) < 0.2 or tt-math.floor(tt) > 0.3:
    time.sleep(0.01)
    tt = time.time()
print('Latching at '+str(tt))
if not op.nosync:
    # waits for the next pps to happen (at time math.ceil(tt))
    # then sets the time for the subsequent pps (at time math.ceil(tt) + 1.0)
    u.set_time_unknown_pps(uhd.time_spec(math.ceil(tt)+1.0))
else:
    u.set_time_now(uhd.time_spec(tt))
# wait 1 sec to ensure the time registers are in a known state
time.sleep(1)

# create flowgraph
fg = gr.top_block()

dirs = [os.path.join(op.dir, ch) for ch in op.chs]
if op.dec > 1:
    taps = firdes.low_pass_2(1.0,
                             op.samplerate,
                             op.samplerate/op.dec/2.0,
                             0.2*(op.samplerate/op.dec),
                             80.0,
                             window=firdes.WIN_BLACKMAN_hARRIS)

    lpfs = [filter.freq_xlating_fir_filter_ccf(op.dec,taps,0.0,op.samplerate) for k in range(nchs)]
    dsts = [drf.digital_rf_sink(
                d, gr.sizeof_gr_complex, op.subdir_cadence_s, op.file_cadence_ms,
                op.samplerate/op.dec, 'THIS_UUID_LACKS_ENTROPY', True, 1,
                op.stop_on_dropped,
            ) for d in dirs]

    for k in range(nchs):
        fg.connect((u, k), (lpfs[k], 0), (dsts[k], 0))

        # create metadata dir, dmd object, and write channel metadata
        md_dir = os.path.join(dirs[k], 'metadata')
        if not os.path.exists(md_dir):
            os.makedirs(md_dir)
        mdo = dmd.write_digital_metadata(
            metadata_dir=md_dir,
            subdirectory_cadence_seconds=op.subdir_cadence_s,
            file_cadence_seconds=1,
            samples_per_second=op.samplerate,
            file_name='metadata',
        )
        md = metadata_dict.copy()
        md.update(
            sample_rate=float(op.samplerate/op.dec),
            sample_period_ps=int(1000000000000/(op.samplerate/op.dec)),
            center_frequencies=np.array([op.centerfreqs[k]]).reshape((1, -1)),
            t0=st,
            n_channels=1,
            itemsize=gr.sizeof_gr_complex,
            dtype='<f4',
            usrp_id=mboards_bychan[k],
            usrp_subdev=subdevs_bychan[k],
            usrp_gain=op.gains[k],
            usrp_stream_args=','.join(op.stream_args),
        )
        mdo.write(
            samples=int(st*op.samplerate/op.dec),
            data_dict=md,
        )
else:
    dsts = [drf.digital_rf_sink(
                d, 2*gr.sizeof_short, op.subdir_cadence_s, op.file_cadence_ms,
                op.samplerate, 'THIS_UUID_LACKS_ENTROPY', True, 1,
                op.stop_on_dropped,
            ) for d in dirs]

    for k in range(nchs):
        fg.connect((u, k), (dsts[k], 0))

        # create metadata dir, dmd object, and write channel metadata
        md_dir = os.path.join(dirs[k], 'metadata')
        if not os.path.exists(md_dir):
            os.makedirs(md_dir)
        mdo = dmd.write_digital_metadata(
            metadata_dir=md_dir,
            subdirectory_cadence_seconds=op.subdir_cadence_s,
            file_cadence_seconds=1,
            samples_per_second=op.samplerate,
            file_name='metadata',
        )
        md = metadata_dict.copy()
        md.update(
            sample_rate=float(op.samplerate),
            sample_period_ps=int(1000000000000/op.samplerate),
            center_frequencies=np.array([op.centerfreqs[k]]).reshape((1, -1)),
            t0=st,
            n_channels=1,
            itemsize=2*gr.sizeof_short,
            dtype='<i2',
            usrp_id=mboards_bychan[k],
            usrp_subdev=subdevs_bychan[k],
            usrp_gain=op.gains[k],
            usrp_stream_args=','.join(op.stream_args),
        )
        mdo.write(
            samples=int(st*op.samplerate),
            data_dict=md,
        )

fg.start()

if et0 is None:
    fg.wait()
else:
    while(time.time() < et0):
       time.sleep(1)

fg.stop()
