0. Install the nano text editor

sudo yum install nano

1. Install EPEL

sudo yum --enablerepo=extras install epel-release

2. Install GCC 9

sudo yum install --enablerepo=extras centos-release-scl

sudo yum install devtoolset-9

Accept the GPG signature if necessary:

合計                                                                        36 MB/s | 161 MB  00:00:04     
file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-SIG-SCLo から鍵を取得中です。
Importing GPG key 0xF2EE9D55:
 Userid     : "CentOS SoftwareCollections SIG (https://wiki.centos.org/SpecialInterestGroup/SCLo) <security@centos.org>"
 Fingerprint: c4db d535 b1fb ba14 f8ba 64a8 4eb8 4e71 f2ee 9d55
 Package    : centos-release-scl-rh-2-3.el7.centos.noarch (@extras)
 From       : /etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-SIG-SCLo
上記の処理を行います。よろしいでしょうか？ [y/N]y

Append

source /opt/rh/devtoolset-9/enable

to your .bashrc file. Log-out and log-in again, then verify the gcc:

[chris@arct01 ~]$ ssh alma-sp-dev04.mtk.nao.ac.jp
Last login: Fri Oct  2 02:46:23 2020 from arct01.mtk.nao.ac.jp
[chris@alma-sp-dev04 ~]$ gcc --version
gcc (GCC) 9.3.1 20200408 (Red Hat 9.3.1-2)

3. Install czmq

sudo yum install czmq-devel

Accept the GPG key:

Importing GPG key 0x352C64E5:
 Userid     : "Fedora EPEL (7) <epel@fedoraproject.org>"
 Fingerprint: 91e9 7d7c 4a5e 96f1 7f3e 888f 6a2f aea2 352c 64e5
 Package    : epel-release-7-11.noarch (@extras)
 From       : /etc/pki/rpm-gpg/RPM-GPG-KEY-EPEL-7
上記の処理を行います。よろしいでしょうか？ [y/N]y

4. Install git

sudo yum install git

5. Install cmake3

sudo yum install cmake3

6. Install nasm

sudo yum install nasm

7. Manually build/install FPZIP

git clone https://github.com/LLNL/fpzip.git

cd fpzip

mkdir build

cd build

cmake3 ..

cmake3 --build . --config Release

sudo make install

8. Install wget

sudo yum install wget

9. Install zlib

sudo yum install zlib-devel

10. Manually build/install OpenEXR (the version in CentOS 7 is too old)

Issue "cd" to return to your home directory

wget https://github.com/AcademySoftwareFoundation/openexr/archive/v2.5.3.tar.gz

tar zxvf v2.5.3.tar.gz

cd openexr-2.5.3

mkdir build

cd build

cmake3 ..

make

sudo make install

11. Manually build (no install is needed) uWebSockets

Issue "cd" to return to your home directory

git clone --recursive https://github.com/uNetworking/uWebSockets.git

cd uWebSockets/uSockets

make

cd ..

make

12. Download the free Intel SPMD Compiler (ispc)

Issue "cd" to return to your home directory

wget https://github.com/ispc/ispc/releases/download/v1.14.1/ispc-v1.14.1-linux.tar.gz

tar zxvf ispc-v1.14.1-linux.tar.gz

sudo cp ispc-v1.14.1-linux/bin/ispc /usr/local/bin/

rm -rf ispc-v1.14.1-linux*

Verify ispc:

ispc --version
Intel(r) Implicit SPMD Program Compiler (Intel(r) ISPC), 1.14.1 (build commit 88118b90d82b2670 @ 20200828, LLVM 10.0.1)

13. Install the free Intel Integrated Performance Primitives (IPP)

Visit https://software.intel.com/en-us/intel-ipp , create a free account and download/install a stand-alone IPP

Append

source /opt/intel/ipp/bin/ippvars.sh intel64

to your .bashrc

Log-out, log-in and verify IPP

echo $IPPROOT
/opt/intel/compilers_and_libraries_2020.2.254/linux/ipp

14. Install Boost headers

sudo yum install boost-devel

15. Install bzip

sudo yum install bzip2

16. Build/install jemalloc

wget https://github.com/jemalloc/jemalloc/releases/download/5.2.1/jemalloc-5.2.1.tar.bz2

bunzip2 jemalloc-5.2.1.tar.bz2

tar xvf jemalloc-5.2.1.tar

cd jemalloc-5.2.1

./configure

make

sudo make install

17. Adjust the environment variables in the .bashrc

Add

export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig
export LD_LIBRARY_PATH=/usr/local/lib64:$LD_LIBRARY_PATH

to .bashrc and re-login

18. Install OpenSSL

sudo yum install openssl-devel

19. Install cURL

sudo yum install curl-devel

20. Install sqlite

sudo yum install sqlite-devel

21. Install PostgreSQL client library

sudo yum install postgresql-devel

22. Clone/set-up the FITSWebQL project

git clone https://github.com/jvo203/FITSWebQL.git

cd FITSWebQL

wget http://jvo.nao.ac.jp/~chris/splatalogue_v3.db

make gcc

23. Run FITSWebQL

./fitswebql

and point a web browser to the server port 8080, i.e.

http://<server>:8080

or 

http://localhost:8080 if running locally.

*** To quit FITSWebQL press Ctrl-C from the command-line terminal. ***
