# FITSWebQL SE
a cleaned-up C/C++ Supercomputer Edition of FITSWebQL, fusing the best elements of the previous versions: Rust (https://github.com/jvo203/fits_web_ql) and C/C++ (http://jvo.nao.ac.jp/~chris/fitswebql.html).

Although Rust is certainly extremely stable, there are still some cases where C/C++ outperforms by a large margin, especially when combined with Intel performance libraries.

# status
work-in-progress

# requirements
a compiler with support for _GLIBCXX_PARALLEL:

* Intel C/C++ version 19-or-higher for the best performance (which bundles IPP)

* alternatively a recent gcc/clang++ with C++17-or-higher plus a manual installation of a free Intel IPP
