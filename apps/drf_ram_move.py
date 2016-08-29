#!/usr/bin/env python
import os
import sys
import time
import glob
import filecmp
import shutil
import signal
import traceback
from argparse import ArgumentParser
from subprocess import check_call
from multiprocessing import Pool
#
# Script to move all drf directories from fast disk to slower permanent storage.
#
# This reduces the amount of dropped packets with usrps to a bare minimum and
# also isolates the disk latency from the recording better.
#
# (c) 2015 Juha Vierinen
# (c) 2016 Ryan Volz
#
# For example, you can use a ram disk as a fast fisk.
#
# sudo mkdir -p /ram
# sudo mkdir -p /data0
# sudo mount -t tmpfs -o size=4000m tmpfs /ram
#
parser = ArgumentParser()
parser.add_argument('ramdir', nargs='?', default='/ram/ringbuffer',
    help='RAM buffer directory',
)
parser.add_argument('savedir', nargs='?', default='/data0/persistent',
    help='Persistent data directory',
)

args = parser.parse_args()

ramfs = os.path.normpath(args.ramdir)
hdfs = os.path.normpath(args.savedir)

if not os.path.isdir(ramfs):
    raise ValueError('RAM buffer directory does not exist!')
if not os.path.isdir(hdfs):
    os.makedirs(hdfs)

def move_channel(d):
    srcdir = os.path.join(ramfs, d)
    destdir = os.path.join(hdfs, d)

    # create directory on persistent drive
    if not os.path.isdir(destdir):
        os.mkdir(destdir)
        shutil.copystat(srcdir, destdir)

    # copy all metadata to persistent drive
    for mdpath in glob.iglob(os.path.join(srcdir, '*.h5')):
        mdname = os.path.basename(mdpath)
        destpath = os.path.join(destdir, mdname)
        if not os.path.exists(destpath) or not filecmp.cmp(mdpath, destpath):
            check_call(('cp', '-a', mdpath, destpath))

    # go through all subdirectories in temporal order
    subnames = os.listdir(srcdir)
    subdirs = [n for n in subnames if os.path.isdir(os.path.join(srcdir, n))]
    subdirs.sort()
    n_subdirs = len(subdirs)

    for idx, s in enumerate(subdirs):
        srcsubdir = os.path.join(srcdir, s)
        destsubdir = os.path.join(destdir, s)

        # create identical directory on persistent drive if non-empty
        if os.listdir(srcsubdir) and not os.path.isdir(destsubdir):
            os.mkdir(destsubdir)
            shutil.copystat(srcsubdir, destsubdir)

        h5ls = glob.glob(os.path.join(srcsubdir, '*.h5'))
        h5ls.sort()

        # if latest directory, leave last two files alone, because they
        # might still be written to.
        if idx == (n_subdirs - 1):
            if len(h5ls) < 3:
                h5ls = []
            else:
                h5ls = h5ls[0:-2]

        # move all files (except the last two files of newest directory) to
        # persistent drive
        for h5 in h5ls:
            sys.stdout.write('*')
            sys.stdout.flush()
            check_call(('cp', '-a', h5, destsubdir))
            os.remove(h5)

        # if not latest directory, then it should be empty and we can remove it
        if idx != (n_subdirs - 1):
            try:
                os.rmdir(srcsubdir)
            except OSError:
                # if it's not empty, continue anyway
                pass

def run_move_channel(d):
    try:
        return move_channel(d)
    except:
        raise Exception(''.join(traceback.format_exception(*sys.exc_info())))

# ignore interrupts in each worker process
def init_worker():
    signal.signal(signal.SIGINT, signal.SIG_IGN)
pool = Pool(initializer=init_worker)
try:
    # indefinitely repeat shoveling operation
    while True:
        sys.stdout.write('.')
        sys.stdout.flush()
        # go through each directory on ramfs
        names = os.listdir(ramfs)
        dirs = [n for n in names if os.path.isdir(os.path.join(ramfs, n))]
        pool.map(run_move_channel, dirs)
        time.sleep(1)
except KeyboardInterrupt:
    pool.terminate()
