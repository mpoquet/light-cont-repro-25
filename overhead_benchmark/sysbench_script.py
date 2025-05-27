#!/usr/bin/env python3
import argparse
import re
import sys

import benchmark

PARSERS = {
    'cpu': re.compile(r'^\s+events per second:\s+(\d+(?:\.\d+))\s*$'),
    'memory': re.compile(r'^.*\s+MiB transferred\s+\((\d+(?:\.\d+))\s+MiB/sec\s*\)\s*$'),
    'fileio': re.compile(r'^\s+written, MiB/s:\s+(\d+(?:\.\d+))\s*$')
}

def parse_file(file, bench, context):
    lines = []
    r = PARSERS[bench]

    for line in file:
        m = r.match(line)
        if m is not None:
            lines.append(f"{context},{bench},{m.group(1)}")

    return lines

def main():
    parser = argparse.ArgumentParser(description='parse sysbench outputs')
    parser.add_argument('-b', metavar='BENCH', choices=['cpu', 'memory', 'fileio'], help='benchmark type to parse', required=True)
    parser.add_argument('-c', metavar='CONTEXT', choices=benchmark.ENGINES, help='execution context', required=True)
    parser.add_argument('-f', metavar='FILE', nargs='+', type=argparse.FileType('r'), help='parse files instead of stdin')
    parser.add_argument('-o', metavar='FILE', type=argparse.FileType('a', encoding='utf-8'), help='append results to FILE instead of stdout')
    args = parser.parse_args()

    lines = []
    files = args.f
    if files is None:
        files = [sys.stdin]

    for f in files:
        lines.extend(parse_file(f, args.b, args.c))

    if len(lines) > 0:
        of = args.o
        if of is None:
            of = sys.stdout
        print('\n'.join(lines), file=of)

if __name__ == '__main__':
    main()
