#!/bin/bash

# this is a surprise tool that will help us later

run_benchmark() {
    echo "Benchmark $1:"
    time ./test $1
    echo
}

if ! cc -Wall -Werror number_benchmark.c cj.o -o test; then
    exit 1
fi

run_benchmark "1"
run_benchmark "0.00000000001"
run_benchmark "3.1415926"
run_benchmark "1.23456789012345678901234567890123456789012345678901234567891234"
run_benchmark "3.456e999"
run_benchmark "3.456e-999"

rm test
