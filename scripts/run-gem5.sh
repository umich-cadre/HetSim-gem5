#!/bin/bash
set -e

APP=${APP:-"workq_mutex"}
MODE=${MODE:-"EMU_TRACE"}

if [[ $MODE == "EMU_TRACE" ]]; then
    tre_en=1
elif [[ $MODE == "SIM" ]]; then
    tre_en=0
else
    echo "MODE must be set to \"EMU_TRACE\" or \"SIM\""
    exit 1
fi

if [[ -z $DEBUGFLAGS ]]; then
    debug_en=0
else
    debug_en=1
fi

bin_path="../example/app/build/$APP"
if [[ ! -f $bin_path ]]; then
    echo "$bin_path does not exist!"
    exit 1
fi

# Build sim/defs Python module.
cd ../example/sim/inc && make && cd -
# Build params Python module.
cd ../example/model && make && cd -

# Launch gem5.
cd ../gem5
if [[ $debug_en -eq 1 ]]; then
    ./build/ARM/gem5.opt \
        --debug-flags=${DEBUGFLAGS} \
        --remote-gdb-port=0 \
        ../example/model/target.py \
        ${bin_path} \
        ${tre_en} \
    2>&1 | tee ./m5out/run.log
else
    ./build/ARM/gem5.opt \
        --remote-gdb-port=0 \
        ../example/model/target.py \
        ${bin_path} \
        ${tre_en} \
    2>&1 | tee ./m5out/run.log
fi
