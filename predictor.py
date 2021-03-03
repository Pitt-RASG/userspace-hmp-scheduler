#!/usr/bin/env python3
import sys
import numpy as np
from cffi import FFI
from pickle import load

def main():
    ffi = FFI()
    ffi.cdef("""
    typedef int (*predict_phase)(long, long, long, long, long, int);
    int scheduler_main(char *argv[], predict_phase cb);
    """)
    lib = ffi.dlopen("./libschedule.so")

    model = load(open("phases-model.pkl", "rb"))
    scaler = load(open("phases-scaler.pkl", "rb"))

    @ffi.callback("int(long,long,long,long,long,int)")
    def run_prediction(p1, p2, p3, p4, p5, cluster):
        pmcs = [[p3, p5, p1, p4, p2, cluster]] # order is based on how the ML was trained
        scaled_pmcs = scaler.transform(pmcs)
        return model.predict(scaled_pmcs)[0]

    # setup subprocess argv
    argv = [ffi.new("char[]", x) for arg in sys.argv[2:]]
    argv.append(ffi.NULL)

    # run it
    lib.scheduler_main(argv, run_prediction)

if __name__ == "__main__":
    main()
