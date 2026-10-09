#!/usr/bin/env python3
"""Compile the real renderer's query-read block against a CPU mock GL API."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/renderer.cpp').read_text()
a = source.index('    int rq = (gpuQi + 1) % 4;   // issued three frames ago')
b = source.index('\n  if (!stampQ[0][0]', a)
read_block = source[a:b].rsplit('\n  }', 1)[0]
assignment = next(line.strip() for line in source.splitlines() if 'stampUsed[gpuQi] =' in line)
# A mock cannot prove hardware timing, but it can prove no read occurs while unavailable.
code = r'''
#include "benchmark_metrics.h"
#include <cassert>
#include <cstdio>
using GLuint = unsigned; using GLuint64 = uint64_t; using GLint = int;
constexpr int GL_QUERY_RESULT_AVAILABLE = 1, GL_QUERY_RESULT = 2;
GLuint gpuQ[4] = {1,2,3,4}; uint64_t gpuQFrame[4] = {1,2,3,4};
bool gpuQUsed[4] = {}; int gpuQi = 0; float gpuMs = -1;
benchmark::GpuSample gpuSample;
bool ready = false; int availabilityReads = 0, resultReads = 0;
void glGetQueryObjectiv(GLuint q, int what, GLint* out) { assert(q == 2 && what == GL_QUERY_RESULT_AVAILABLE); ++availabilityReads; *out = ready; }
void glGetQueryObjectui64v(GLuint q, int what, GLuint64* out) { assert(ready && q == 2 && what == GL_QUERY_RESULT); ++resultReads; *out = 12500000; }
void poll() {
''' + read_block + r'''
}
bool syncTiming = false, stampUsed[4] = {}; GLuint stampQ[4][12] = {};
void markStamps() {
''' + assignment + r'''
}
int main() {
  poll(); assert(availabilityReads == 0 && resultReads == 0 && gpuSample.id == 0);
  gpuQUsed[1] = true;
  poll(); assert(availabilityReads == 1 && resultReads == 0 && gpuSample.id == 0 && gpuQUsed[1]);
  ready = true; poll(); assert(resultReads == 1 && gpuSample.id == 1 && gpuSample.frame == 2 && gpuSample.ms == 12.5 && !gpuQUsed[1]);
  poll(); assert(resultReads == 1 && gpuSample.id == 1); // cannot replay a completed sample
  gpuQUsed[1] = true; gpuQFrame[1] = 6; ready = false;
  poll(); assert(resultReads == 1 && gpuSample.id == 1 && gpuSample.frame == 2);
  ready = true; poll(); assert(resultReads == 2 && gpuSample.id == 2 && gpuSample.frame == 6);
  stampQ[0][0] = 42; syncTiming = true; markStamps(); assert(!stampUsed[0]);
  syncTiming = false; markStamps(); assert(stampUsed[0]);
  stampQ[0][0] = 0; markStamps(); assert(!stampUsed[0]);
  puts("production query read block: unavailable/stale/fresh samples and sync stamp readiness passed");
}
'''
parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='c++')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='solace-query-mock-') as tmp:
    cpp, binary = Path(tmp) / 'mock.cpp', Path(tmp) / 'mock'
    cpp.write_text(code)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
    subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-pedantic', *flags, '-I', str(root/'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
