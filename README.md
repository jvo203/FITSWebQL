# FITSWebQL SE
A cleaned-up C/C++ Supercomputer Edition of FITSWebQL, fusing the best elements of the previous versions: Rust (https://github.com/jvo203/fits_web_ql), C/C++ (http://jvo.nao.ac.jp/~chris/fitswebql.html) and SubaruWebQL.

Although Rust is extremely stable there are still some cases where C/C++ outperforms by a large margin, especially when combined with Intel high-performance libraries.

# status
work-in-progress

An installation manual for [CentOS 7](CentOS7.md) might need to be adjusted for other operating systems. [need for speed]: we recommend Intel Clear Linux (even, or especially on AMD CPUs).

# requirements
1. Intel Integrated Performance Primitives (IPP): https://software.intel.com/en-us/intel-ipp

2. a compiler with support for _GLIBCXX_PARALLEL:

    * paid-for Intel C/C++ version 19-or-higher for the best performance (which bundles IPP)

    * alternatively a recent gcc/clang++ with C++17 plus a manual installation of the free Intel IPP: https://software.intel.com/en-us/intel-ipp

3. an open-source Intel SPMD Program Compiler (ispc): https://ispc.github.io

4. the 809MB-large spectral lines database needs to be downloaded from http://jvo.nao.ac.jp/~chris/splatalogue_v3.db and placed inside the FITSWebQL directory (for example "wget http://jvo.nao.ac.jp/~chris/splatalogue_v3.db")

