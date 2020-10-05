#!/bin/bash
# -*- tab-width: 4; encoding: utf-8; -*-

## @author Subhankar Pal
## @brief Convenience script to build gem5
## @version 1.0

FLAVOR=opt
ROOTDIR=`pwd`/..
CPUTYPE=TimingSimpleCPU
VERBOSE=${VERBOSE:-1}

source $ROOTDIR/scripts/init.sh

cd $ROOTDIR/gem5
info "Starting gem5 build for $CPUTYPE"
scons_cmd="scons build/ARM/gem5.$FLAVOR CPU_MODELS=$CPUTYPE -j`nproc`"
if [ $VERBOSE -eq 0 ]; then
    eval $scons_cmd > /dev/null 2>&1
else
    eval $scons_cmd
fi
if [ $? -eq 0 ]; then
    info "gem5 build succeeded"
else
    warn "gem5 build failed"
    exit 1
fi

info "Compiling m5threads library"
cd $ROOTDIR/m5threads/tests
make ../pthread.o
if [ $? -gt 0 ] ; then
    warn "m5threads compilation failed"
    exit
fi

info "build-gem5.sh successfully exiting"
exit 0
