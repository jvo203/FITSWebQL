SRC = src/main_uWebSockets_v0.16.cpp src/fits.cpp src/classifier.cpp src/json.c lz4/lz4.c lz4/lz4hc.c
INC = -I/usr/include/postgresql -Ilz4 -I/home/chris/uWebSockets/src -I/home/chris/uWebSockets/uSockets/src
#-Ibm-3.20.0/src
DEF = -DLIBUS_NO_SSL -DHAVE_INLINE -DFORCE_AVX=ON -DDEVELOPMENT -DLOCAL -DCLUSTER
#-D_GLIBCXX_PARALLEL
LIBS = -lstdc++fs -lsqlite3 -lcurl -lcrypto -l:libpq.so.5 -lssl -lz -L/usr/local/lib64 -lzfp -l:libnuma.so.1 -lpthread /home/chris/uWebSockets/uSockets/*.o -lczmq
#-luWS
IPP = -L${IPPROOT}/lib/intel64 -lippi -lippdc -lipps -lippcore
JEMALLOC = -L`jemalloc-config --libdir` -Wl,-rpath,`jemalloc-config --libdir` -ljemalloc `jemalloc-config --libs`
TARGET=fitswebql

#disabled jemalloc for now as it seems to have problems with ZFP private views...mutable or not!

dev:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	icpc -g -O3 -xHost -mcmodel large -qopenmp -qopenmp-simd -qopt-streaming-stores auto -funroll-loops -ipo -std=c++17 -fp-model fast -qopt-report=5 -qopt-report-phase=vec $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) -ipp

#$(JEMALLOC) 

llvm:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	clang++ -march=native -g -O3 -std=c++17 -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize -Rpass=loop-vectorize $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) $(IPP)
	
#$(JEMALLOC)

gcc:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	g++ -march=native -g -O3 -std=c++17 -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) $(IPP)

#$(JEMALLOC)

darwin:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	/usr/local/opt/llvm/bin/clang++ -march=native -g -O3 -std=c++17 -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize -Rpass=loop-vectorize $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) -L/usr/local/opt/llvm/lib $(IPP)

#$(JEMALLOC)

#	cargo run -- ~/NAO/NRO/SF/orion_12co_all_SF7.5arcsec_dV1.0kms.fits
#	https://zeromq-dev.zeromq.narkive.com/VMD0bZ8X/no-udp-broadcast-message-received-using-czmq-zbeacon-on-raspberrypi3
