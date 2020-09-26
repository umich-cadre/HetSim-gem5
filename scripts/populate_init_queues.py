#!/usr/bin/env python

# reads in the user spec file and creates software queues corresponding to
# __push() and __pop() calls in the spec file for the emulation library

from collections import OrderedDict
import json
import sys

# these should not need to change
emu_src_cpp = "../emu/src/util.cpp"
tre_src_cpp = "../gem5/src/cpu/tre/tre.cc"

sys.path.append('../example/model')
# import model parameters
import params
sys.path.append('../example/sim/inc')
# import model defines
import defs
# extract as local variables
for define in dir(defs):
    if define[0] != '_':        # This filters out the swig stuff.
        exec(define + " = int(defs." + define + ") & 0xFFFFFFFF")
for define in dir(params):
    if define[0] != '_':        # This filters out the swig stuff.
        exec(define + " = int(params." + define + ")")

def overwrite_init_queues_call(conn_list, filename, structname):
    newlines = []

    infile = open(filename, 'r')
    assert infile, "Couldn't open file: " + filename
    data = infile.readlines()
    infile.close()

    done = False
    for line in data:
        if done:
            if "// end generated code for init_queues" in line:
                newlines.append(line)
                done = False
            else:
                continue
        elif "// begin generated code for init_queues" in line:
            newlines.append(line)
            for (source, sink) in conn_list:
                newlines.append("    queues.insert(std::make_pair(std::make_pair(" + str(source) + ", " + str(sink) +
                    "), new " + structname + "(depth)));\n")
            done = True
        else:
            newlines.append(line)

    outfile = open(filename, 'w')
    assert outfile, "Couldn't open file: " + filename
    for line in newlines:
        outfile.write(line.rstrip('\n') + '\n')
    outfile.close()

# read json file
with open('../spec/spec.json') as f:
  data = json.load(f, object_pairs_hook=OrderedDict)

queues = []
# gather information about connections between hardware queues
for (k1, v1) in data.items():
    if k1 == "queues":
        for (k2, v2) in v1.items():
            for source_id in v2["source"]:
                for sink_id in v2["sink"]:
                    # print("%u <-> %u" % (source_id, sink_id))
                    queues.append((source_id, sink_id))

overwrite_init_queues_call(queues, emu_src_cpp, "q_intfc_t")
overwrite_init_queues_call(queues, tre_src_cpp, "TREQueue")
