# 0. Install wget

sudo swupd bundle-add wget

# 1. Install GCC

sudo swupd bundle-add c-basic

# 2. Install git

sudo swupd bundle-add git

# 3. Install ØMQ

sudo swupd bundle-add devpkg-libzmq

# 4. Manually build/install czmq

git clone git://github.com/zeromq/czmq.git

cd czmq

./autogen.sh && ./configure && make check

sudo make install

sudo ldconfig

cd ..

# 5. Manually build/install FPZIP

git clone https://github.com/LLNL/fpzip.git

cd fpzip

mkdir build

cd build

cmake3 ..

cmake3 --build . --config Release

sudo make install

cd

# 6. Install zlib

sudo swupd bundle-add devpkg-zlib

# 7. Manually build/install OpenEXR (the version in CentOS 7 is too old)

Issue "cd" to return to the home directory

wget https://github.com/AcademySoftwareFoundation/openexr/archive/v2.5.3.tar.gz

tar zxvf v2.5.3.tar.gz

cd openexr-2.5.3

mkdir build

cd build

cmake3 ..

make

sudo make install

cd

# 8. Manually build (no install is needed) uWebSockets

Issue "cd" to return to your home directory

git clone --recursive https://github.com/uNetworking/uWebSockets.git

cd uWebSockets/uSockets

make

cd ..

make

cd

# 9. Download the free Intel SPMD Compiler (ispc)

Issue "cd" to return to your home directory

wget https://github.com/ispc/ispc/releases/download/v1.14.1/ispc-v1.14.1-linux.tar.gz

tar zxvf ispc-v1.14.1-linux.tar.gz

sudo cp ispc-v1.14.1-linux/bin/ispc /usr/local/bin/

rm -rf ispc-v1.14.1-linux*

Verify ispc:

ispc --version
Intel(r) Implicit SPMD Program Compiler (Intel(r) ISPC), 1.14.1 (build commit 88118b90d82b2670 @ 20200828, LLVM 10.0.1)

# 10. Install the free Intel Integrated Performance Primitives (IPP)

Visit https://software.intel.com/en-us/intel-ipp , create a free account and download/install a stand-alone IPP

Append

source /opt/intel/ipp/bin/ippvars.sh intel64

to your .bashrc

Log-out, log-in and verify IPP

echo $IPPROOT
/opt/intel/compilers_and_libraries_2020.2.254/linux/ipp

# 11. Install bzip2

sudo swupd bundle-add devpkg-bzip2

# 12. Install Boost headers

sudo swupd bundle-add devpkg-boost

# 13. Build/install jemalloc

Issue "cd" to return to the home directory

wget https://github.com/jemalloc/jemalloc/releases/download/5.2.1/jemalloc-5.2.1.tar.bz2

bunzip2 jemalloc-5.2.1.tar.bz2

tar xvf jemalloc-5.2.1.tar

cd jemalloc-5.2.1

./configure

make

sudo make install

cd

# 14. Adjust the environment variables in the .bashrc

Add

export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$LD_LIBRARY_PATH

to .bashrc and re-login

# 15. Install OpenSSL

sudo swupd bundle-add devpkg-openssl

# 16. Install cURL

sudo swupd bundle-add devpkg-curl

# 17. Install sqlite

sudo swupd bundle-add devpkg-sqlite-autoconf

# 18. Install PostgreSQL client library

sudo swupd bundle-add devpkg-postgresql

# 19. Build/install x265

wget http://anduin.linuxfromscratch.org/BLFS/x265/x265_3.4.tar.gz

tar zxvf x265_3.4.tar.gz

cd x265_3.4

mkdir -p build

cd build

cmake ../source

make

sudo make install

cd

# 20. Compile FITSWebQL

Issue "cd" to return to the home directory

git clone https://github.com/jvo203/FITSWebQL.git

cd FITSWebQL

wget http://jvo.nao.ac.jp/~chris/splatalogue_v3.db

make gcc

# 21. Run FITSWebQL

./fitswebql

and point a web browser to the server port 8080, i.e.

http://<server>:8080

or 

http://localhost:8080 if running locally.

## *** To quit FITSWebQL press Ctrl-C from the command-line terminal. ***
