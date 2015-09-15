#!/usr/bin/env python
import os
import time
import glob
#
# Script to move all drf directories from fast disk to slower permanent storage.
#
# This reduces the amount of dropped packets with usrps to a bare minimum and
# also isolates the disk latency from the recording better.
# 
# (c) 2015 Juha Vierinen
# 
# For example, you can use a ram disk as a fast fisk.
#
# sudo mkdir -p /ram
# sudo mkdir -p /data0
# sudo mount -t tmpfs -o size=4000m tmpfs /ram
#
ramfs="/ram/ringbuffer"
hdfs="/data0/persistent"
# get subdirectories

def move_files():
    # go through each directory on ramfs
    dirs = next(os.walk(ramfs))[1]
    for d in dirs:
        # create directory on persistent drive
        print("copying %s"%(d))
        if not os.path.isdir("%s/%s"%(hdfs,d)):
            print("creating dir")
            os.system("mkdir -p %s/%s"%(hdfs,d))

        # copy all metadata to persistent drive
        os.system("cp %s/%s/*.h5 %s/%s/"%(ramfs,d,hdfs,d))
        
        # go through all subdirectories in temporal order
        subdirs = next(os.walk("%s/%s"%(ramfs,d)))[1]
        subdirs.sort()
        n_subdirs = len(subdirs)

        for idx,s in enumerate(subdirs):
            # create identical directory on persistent drive
            print("%s %d/%d"%(s,idx,n_subdirs))
            if not os.path.isdir("%s/%s/%s"%(hdfs,d,s)):
                print("creating dir")
                dir_fl = glob.glob("%s/%s/%s/*"%(ramfs,d,s))
                # create directory on slow filesystem if there are files in subdirectory
                if len(dir_fl) > 0:
                    os.system("mkdir -p %s/%s/%s"%(hdfs,d,s))

            h5ls = glob.glob("%s/%s/%s/*.h5"%(ramfs,d,s))
            h5ls.sort()

            # if latest directory, leave last two files alone, because they
            # might still be written to.
            if idx == (n_subdirs-1):
                print("last file %s"%(h5ls[len(h5ls)-1]))
                if len(h5ls) < 3:
                    h5ls = []
                else:
                    h5ls = h5ls[0:(len(h5ls)-2)]
            # move all files (except the last two files of newest directory) to
            # persistent drive
            for h5 in h5ls:
                print("cp %s %s/%s/%s/"%(h5,hdfs,d,s))
                os.system("cp %s %s/%s/%s/"%(h5,hdfs,d,s))
                print("rm %s"%(h5))
                os.system("rm %s"%(h5))

# indefinitely repeat shoveling operation
while True:
    move_files()
    time.sleep(1)


