#!/usr/bin/env python

"""
Graph kheap for larson
"""

from matplotlib import pyplot as plt
import os
import re
import subprocess
import sys

allocators = ["kheap", "libc", "a2alloc"]
#allocators = ["kheap", "a2alloc"]
colors = ["red", "blue", "green"]

def run_command(command):
    p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ret = os.waitpid(p.pid, 0)[1]
    if ret != 0:
        return p.stderr.read()
    else:
        return p.stdout.read()

def get_folder(benchmark, allocator):
    return os.getcwd() + "/%s/Results/%s" % (benchmark, allocator)

def parse_larson_results(fname):
    results = {
        "throughput": None,
        "mem_used": None,
        "mem_req": None,
        "mem_ratio": None
    }

    p1 = re.compile(r"Throughput =\s+(\d+) operations per second")
    p2 = re.compile(r"Memory used = (\d+) bytes, required (\d+), ratio ([0-9]+\.[0-9]+)")

    with open(fname) as fp:
        for line in fp:
            m = p1.match(line)
            if m is not None:
                results["throughput"] = int(m.group(1))
                continue
            m = p2.match(line)
            if m is not None:
                results["mem_used"] = int(m.group(1))
                results["mem_req"] = int(m.group(2))
                results["mem_ratio"] = float(m.group(3))
                continue
    return results

def load_larson_data(benchmark, allocator, throughput_pts, mem_ratio_pts):
    dir = get_folder(benchmark, allocator)
    for fname in sorted(os.listdir(dir)):
        if fname == "data":
            continue
        full_fname = os.path.join(dir, fname)
        results = parse_larson_results(full_fname)
        #print results
        throughput_pts[allocator].append(results["throughput"])
        mem_ratio_pts[allocator].append(results["mem_ratio"])

def show_results_larson_throughput(throughput_pts):
    benchmark = "larson"
    x = range(1, len(throughput_pts["kheap"]) + 1)
    print x

    for i, allocator in enumerate(allocators):
        plt.plot(x, throughput_pts[allocator], colors[i], label=allocator, marker='o')

    plt.ylabel("Throughput (operations per second)")
    plt.xlabel("Number of Concurrent Threads")
    plt.title("Throughput on Larson Benchmark")
    plt.legend(loc=2)
    plt.savefig(benchmark + "_throughput.png")
    plt.show()

def show_results_larson_mem_ratio(mem_ratio_pts):
    benchmark = "larson"
    x = range(1, len(mem_ratio_pts["kheap"]) + 1)
    print x

    for i, allocator in enumerate(allocators):
        plt.plot(x, mem_ratio_pts[allocator], colors[i], label=allocator, marker='o')

    plt.ylabel("Memory Ratio")
    plt.xlabel("Number of Concurrent Threads")
    plt.title("Memory Ratio on Larson Benchmark")
    plt.legend(loc=1)
    plt.savefig(benchmark + "_memory.png")
    plt.show()

def show_results_larson():
    throughput_pts = {"kheap": [], "libc": [], "a2alloc": []}
    mem_ratio_pts = {"kheap": [], "libc": [], "a2alloc": []}
    benchmark = "larson"

    for allocator in allocators:
        load_larson_data(benchmark, allocator, throughput_pts, mem_ratio_pts)
        print throughput_pts[allocator]
        print mem_ratio_pts[allocator]

    show_results_larson_throughput(throughput_pts)
    show_results_larson_mem_ratio(mem_ratio_pts)


def load_cache_bench_data(benchmark, allocator):
    dir = get_folder(benchmark, allocator)
    data_fname = os.path.join(dir, "data")
    l = []
    with open(data_fname) as fp:
        for line in fp:
            pt = line.strip().split()[-1]
            l.append(float(pt))
    return l

def show_results_cache_bench(benchmark, title):
    scalability = {"kheap": [], "libc": [], "a2alloc": []}

    # generate data file
    out = run_command(["./graph-%s.pl" % benchmark])

    for allocator in allocators:
        scalability[allocator] = load_cache_bench_data(benchmark, allocator)

    x = range(1, len(scalability["kheap"]) + 1)
    print x

    for i, allocator in enumerate(allocators):
        plt.plot(x, scalability[allocator], colors[i], label=allocator, marker='o')

    plt.ylabel("Runtime (seconds)")
    plt.xlabel("Number of Concurrent Threads")
    plt.title("%s: %s runtimes" % (benchmark, title))
    plt.legend(loc=1)
    plt.savefig(benchmark + ".png")
    plt.show()

def main(benchmarks):
    num_graphed = 0
    if "larson" in benchmarks:
        show_results_larson()
        num_graphed += 1
    if "cache-thrash" in benchmarks:
        show_results_cache_bench("cache-thrash", "active-false")
        num_graphed += 1
    if "cache-scratch" in benchmarks:
        show_results_cache_bench("cache-scratch", "passive-false")
        num_graphed += 1
    if "threadtest" in benchmarks:
        show_results_cache_bench("threadtest", "threadtest")
        num_graphed += 1

    if num_graphed == 0:
        print >>sys.stderr, "No valid benchmarks found to graph"
        return 1

    return 0

if __name__ == "__main__":
    # get arguments
    if len(sys.argv) > 1:
        retval = main(sys.argv[1:])
        sys.exit(retval)
    else:
        print >>sys.stderr, "Error: no benchmark specified. Usage: %s [benchmark-name]+" % sys.argv[0]
        sys.exit(1)
