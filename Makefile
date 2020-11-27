.DEFAULT_GOAL := fitswebql

# detect the OS
UNAME_S := $(shell uname -s)

#CXX = clang++

ifeq ($(UNAME_S),Darwin)
	CXX := clang++
endif

ifeq ($(UNAME_S),Linux)
	#-Ofast does not work with NaN and Inf, unfortunately... and so -fno-finite-math-only is needed
	override CXXFLAGS += -march=native -g -Ofast -fno-finite-math-only -std=c++17 -Wno-register -fopenmp -fopenmp-simd -funroll-loops -ftree-vectorize
endif

ifeq ($(CXX),clang++)
	CXXFLAGS += -Rpass=loop-vectorize
endif

ifeq ($(UNAME_S),Darwin)
	CXXFLAGS := -Xpreprocessor -Ofast -fno-finite-math-only -std=c++17 -Wno-register -funroll-loops -ftree-vectorize -Rpass=loop-vectorize
endif

BEAST = src/shared_state.cpp src/listener.cpp src/websocket_session.cpp src/http_session.cpp
MONGOOSE = mongoose/mongoose.c

SRC = src/webql.ispc src/kalman.cpp src/fits.cpp src/classifier.cpp src/json.c lz4/lz4.c lz4/lz4hc.c src/contours.cpp src/par_msquares.cpp src/main_uWS.cpp
#OBJ = src/webql.o src/kalman.o src/fits.o src/classifier.o src/json.o lz4/lz4.o lz4/lz4hc.o src/contours.o src/par_msquares.o src/main_uWS.o
OBJ := $(SRC:.cpp=.o)
OBJ := $(OBJ:.c=.o)
OBJ := $(OBJ:.ispc=.o)
DEP = $(OBJ:%.o=%.d)
#$(MONGOOSE)
#$(BEAST) 
INC = -I/usr/include/postgresql -Ilz4 -I$(HOME)/uWebSockets/src -I$(HOME)/uWebSockets/uSockets/src
#-Izfp-0.5.5/include -Izfp-0.5.5/array
#-I$(HOME)/uWebSockets/src
#-I$(HOME)/uWebSockets/uSockets/src
#-Imongoose
#-Ibm-3.20.0/src
DEF = -DMG_ENABLE_THREADS -DLIBUS_NO_SSL -DHAVE_INLINE -DFORCE_AVX=ON -DDEVELOPMENT -DLOCAL -DCLUSTER -DNOPRELOAD -DDEBUG
#-D_GLIBCXX_PARALLEL
LIBS = -lsqlite3 -lcurl -lcrypto -lssl -lz -lfpzip  -lpthread -lczmq `pkg-config --libs OpenEXR` -lx265  $(HOME)/uWebSockets/uSockets/*.o
#-lIlmImf -lIlmThread -lHalf
#-lnghttp2_asio

LIBS += -lboost_system

ifeq ($(UNAME_S),Linux)
	LIBS += -l:libpq.so.5 -l:libnuma.so.1 -lboost_thread -lmvec -lm
endif

ifeq ($(UNAME_S),Darwin)
	INC += -I/usr/local/opt/openssl/include -I/usr/local/opt/libpq/include
	LIBS += -L/usr/local/opt/libpq/lib -lpq -lboost_thread-mt -lomp -L/usr/local/opt/openssl/lib
endif 

#-lstdc++fs
#-Lzfp-0.5.5/lib -lzfp
#-L/usr/local/lib64 -lzfp
#$(HOME)/uWebSockets/uSockets/*.o

ifeq ($(UNAME_S),Linux)
	IPP = -L${IPPROOT}/lib/intel64
endif

ifeq ($(UNAME_S),Darwin)
	IPP = -L${IPPROOT}/lib 
endif

IPP += -lippi -lippdc -lipps -lippcore

JEMALLOC = -L`jemalloc-config --libdir` -Wl,-rpath,`jemalloc-config --libdir` -ljemalloc `jemalloc-config --libs`
TARGET = fitswebql

# disabled jemalloc for now as it seems to have problems with ZFP private views...mutable or not!

# include dependencies (all .d files)
-include $(DEP)

%.o: %.ispc
	ispc -g -O3 --pic --opt=fast-math --addressing=32 -o $@ -h $(subst .o,.h,$@) $<

%.o: %.c
	$(CXX) $(CXXFLAGS) $(DEF) $(INC) -MMD -o $@ -c $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEF) $(INC) -MMD -o $@ -c $<

fitswebql: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $^ $(LIBS) $(IPP) $(JEMALLOC)

#OBJ := $(addsuffix .o,$(basename $(SRC))
OBJ := $(SRC:.cpp=.o)
OBJ := $(OBJ:.c=.o)
OBJ := $(OBJ:.ispc=.o)
subst:
	@echo $(OBJ)

clean:
	rm -f src/*.o lz4/*.o src/*.d lz4/*.d fits.h fits.o webql.o webql.h $(TARGET)

dev:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/webql.ispc -o src/webql.o -h src/webql.h
	icpc -g -O3 -xHost -mcmodel large -qopenmp -qopenmp-simd -qopt-streaming-stores auto -funroll-loops -ipo -std=c++17 -fp-model fast -qopt-report=5 -qopt-report-phase=vec $(DEF) $(INC) $(SRC) src/webql.o -o $(TARGET) $(LIBS) -ipp $(JEMALLOC)

llvm:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/webql.ispc -o src/webql.o -h src/webql.h
	clang++ $(CXXFLAGS) -Rpass=loop-vectorize $(DEF) $(INC) $(SRC) src/webql.o -o $(TARGET) $(LIBS) $(IPP) $(JEMALLOC) -lmvec -lm

gcc:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/webql.ispc -o src/webql.o -h src/webql.h
	g++ $(CXXFLAGS) $(DEF) $(INC) $(SRC) src/webql.o -o $(TARGET) $(LIBS) $(IPP) $(JEMALLOC) -lmvec -lm

darwin:
	ispc -g -O3 --pic --opt=fast-math --addressing=32 src/webql.ispc -o src/webql.o -h src/webql.h
	clang++ -Xpreprocessor -Ofast -fno-finite-math-only -std=c++17 -Wno-register -funroll-loops -ftree-vectorize -Rpass=loop-vectorize -I/usr/local/opt/openssl/include $(DEF) $(INC) $(SRC) src/webql.o -o $(TARGET) -lomp -L/usr/local/opt/openssl/lib $(LIBS) $(IPP) $(JEMALLOC)

#/usr/local/opt/llvm/bin/clang++ $(CXXFLAGS) -Rpass=loop-vectorize -I/usr/local/include -I/usr/local/opt/llvm/include -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include -I/usr/local/opt/openssl/include $(DEF) $(INC) $(SRC) fits.o -o $(TARGET) -L/usr/local/lib $(LIBS) -L/usr/local/opt/llvm/lib -L/usr/local/opt/openssl/lib $(IPP) $(JEMALLOC)

omp1:
	/usr/local/opt/llvm/bin/clang++ $(CXXFLAGS) testOpenMP.cpp -L/usr/local/opt/llvm/lib

omp2:
	clang++ -Xpreprocessor -Ofast -fno-finite-math-only -std=c++17 -Wno-register -funroll-loops -ftree-vectorize -Rpass=loop-vectorize testOpenMP.cpp -lomp
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

ippzfp:
	icpc -g -O3 -xHost testIPPZFP.cpp -ipp

resize:
	icpc -g -O3 -xHost testIPPResize.cpp -ipp -lnetpbm

x265:
	g++ -march=native -g -O3 testx265.cpp -lx265

home:
	g++ -march=native -g -O3 testIPPResize.cpp $(IPP)

hist:
	g++ -march=native -g -O3 testHistogram.cpp

mac:
	/usr/local/opt/llvm/bin/clang++ -march=native -g -O3 testIPPResize.cpp $(IPP)

nppi:
	g++ -march=native -O3 -I/usr/local/cuda/include -I/usr/local/cuda/samples/common/inc testNPPIResize.cpp -L/usr/local/cuda/lib64 -lnppig -lnppisu -lculibos -lcudart_static -lpthread -ldl -lrt
