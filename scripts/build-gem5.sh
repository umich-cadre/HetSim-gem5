#!/bin/bash
# -*- tab-width: 4; encoding: utf-8; -*-

## @author Subhankar Pal
## @brief Convenience script to build gem5
## @version 1.0

FLAVOR=opt
ROOTDIR=`pwd`/..
CPUTYPE=TimingSimpleCPU

source $ROOTDIR/scripts/init.sh

cd $ROOTDIR/gem5
info "Starting gem5 build for $CPUTYPE at `pwd` with ${CPUTYPE}..."
scons build/ARM/gem5.$FLAVOR CPU_MODELS=$CPUTYPE -j`nproc`
if [ $? -eq 0 ]; then
    info "gem5 build succeeded!"
else
    warn "gem5 build failed!"
    exit 1
fi

info "Compiling m5op for Arm..."
cd util/m5
make -f Makefile.thumb

info "Compiling m5threads library..."
cd ../../../m5threads/tests
make ../pthread.o
if [ $? -gt 0 ] ; then
    warn "m5threads compilation failed"
    exit
fi
exit 0
