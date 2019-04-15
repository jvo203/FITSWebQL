SRC = src/main.cpp src/fits.cpp src/classifier.cpp src/json.c lz4/lz4.c lz4/lz4hc.c
INC = -I/usr/include/postgresql -Ilz4
#-Ibm-3.20.0/src -I/home/chris/uWebSockets/src -I/home/chris/uSockets-0.1.2/src
DEF = -DLIBUS_NO_SSL -DHAVE_INLINE -D_GLIBCXX_PARALLEL -DFORCE_AVX=ON -DDEVELOPMENT -DLOCAL
LIBS = -lstdc++fs -lsqlite3 -lcurl -lcrypto -lpq -luWS -lssl -lz -lzfp -lnuma -lpthread
IPP = -L${IPPROOT}/lib/intel64 -lippi -lippdc -lipps -lippcore
JEMALLOC = -L`jemalloc-config --libdir` -Wl,-rpath,`jemalloc-config --libdir` -ljemalloc `jemalloc-config --libs`
TARGET=fitswebql

#disabled jemalloc for now as it seems to have problems with ZFP private views...mutable or not!

dev:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	icpc -g -O3 -xCORE-AVX2 -mcmodel large -qopenmp -qopenmp-simd -qopt-streaming-stores auto -funroll-loops -ipo -std=c++17 -fp-model fast -qopt-report=5 -qopt-report-phase=vec $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) -ipp

#$(JEMALLOC) 

llvm:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	clang++ -march=native -g -O3 -std=c++17 -fopenmp=libiomp5 -fopenmp-simd -funroll-loops -ftree-vectorize -Rpass=loop-vectorize $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) $(IPP)
	
#$(JEMALLOC)

gcc:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	g++ -march=native -g -O3 -std=c++17 -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) $(IPP)
	
#$(JEMALLOC)
