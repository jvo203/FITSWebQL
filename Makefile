BEAST = src/shared_state.cpp src/listener.cpp src/websocket_session.cpp src/http_session.cpp
MONGOOSE = mongoose/mongoose.c
SRC = src/main.cpp src/fits.cpp src/classifier.cpp src/json.c lz4/lz4.c lz4/lz4hc.c
#$(MONGOOSE)
#$(BEAST) 
INC = -I/usr/include/postgresql -Ilz4
#-Izfp-0.5.5/include -Izfp-0.5.5/array
#-I$(HOME)/uWebSockets/src
#-I$(HOME)/uWebSockets/uSockets/src
#-Imongoose
#-Ibm-3.20.0/src
DEF = -DMG_ENABLE_THREADS -DLIBUS_NO_SSL -DHAVE_INLINE -DFORCE_AVX=ON -DDEVELOPMENT -DLOCAL -DCLUSTER
#-D_GLIBCXX_PARALLEL
LIBS = -lstdc++fs -lsqlite3 -lcurl -lcrypto -lbsd -l:libpq.so.5 -lssl -lz -l:libnuma.so.1 -lpthread -lczmq -lnghttp2_asio -lboost_system -lIlmImf -lIlmThread -lHalf
#-Lzfp-0.5.5/lib -lzfp
#-L/usr/local/lib64 -lzfp
#$(HOME)/uWebSockets/uSockets/*.o
#-luWS
IPP = -L${IPPROOT}/lib/intel64 -lippi -lippdc -lipps -lippcore
JEMALLOC = -L`jemalloc-config --libdir` -Wl,-rpath,`jemalloc-config --libdir` -ljemalloc `jemalloc-config --libs`
TARGET=fitswebql

#disabled jemalloc for now as it seems to have problems with ZFP private views...mutable or not!

dev:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	icpc -g -O3 -xHost -mcmodel large -qopenmp -qopenmp-simd -qopt-streaming-stores auto -funroll-loops -ipo -std=c++17 -fp-model fast -qopt-report=5 -qopt-report-phase=vec $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) -ipp $(JEMALLOC)

#$(JEMALLOC) 

llvm:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	clang++ -march=native -g -O3 -std=c++17 -Wno-register -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize -Rpass=loop-vectorize $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) $(IPP) $(JEMALLOC)

#$(JEMALLOC)

gcc:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	g++ -march=native -g -O3 -std=c++17 -Wno-register -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) $(IPP) $(JEMALLOC)

#$(JEMALLOC)

darwin:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/fits.ispc -o fits.o -h fits.h
	/usr/local/opt/llvm/bin/clang++ -march=native -g -O3 -std=c++17 -Wno-register -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize -Rpass=loop-vectorize -I/usr/local/include -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include -I/usr/local/Cellar/czmq/4.2.0/include -I/usr/local/Cellar/zeromq/4.3.2/include -I/usr/local/Cellar/boost/1.71.0/include -I/usr/local/opt/openssl/include $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) $(LIBS) -L/usr/local/opt/llvm/lib -L/usr/local/opt/openssl/lib $(IPP)

#$(JEMALLOC)

#	cargo run -- ~/NAO/NRO/SF/orion_12co_all_SF7.5arcsec_dV1.0kms.fits
#	https://zeromq-dev.zeromq.narkive.com/VMD0bZ8X/no-udp-broadcast-message-received-using-czmq-zbeacon-on-raspberrypi3

inet:
	gcc src/inet.c -o inet

beast:
	g++ -march=native -g -O3 -std=c++17 src/main_http_ws_beast.cpp -o fitswebql

cypher:
	mkdir -p ssl
	openssl req -x509 -nodes -days 3650 -newkey rsa:2048 -keyout ssl/server.key -out ssl/server.crt
