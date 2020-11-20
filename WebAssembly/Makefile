.DEFAULT_GOAL := wasm
VERSION = 20.11.20.0

FPZIP=fpzip
SRC=OpenEXR/*.cpp $(FPZIP)/src/*.cpp
INCLUDE=-I./ -I./OpenEXR -I./$(FPZIP)/include -I./$(FPZIP)/src
LIBRARY=
CXXFLAGS=-std=c++11 -O3
LDFLAGS=-lz --llvm-lto 1

.PHONY=clean

EMFLAGS=--bind
EMFLAGS+=-s ALLOW_MEMORY_GROWTH=1
EMFLAGS+=-s NO_EXIT_RUNTIME=1
EMFLAGS+=-s NO_FILESYSTEM=1
#EMFLAGS+=-s USE_PTHREADS=1 -s PROXY_TO_PTHREAD -s WASM_MEM_MAX=2GB
EMFLAGS+=-s USE_ZLIB=1

#EMFLAGS+=-s MODULARIZE=1
#EMFLAGS+=-s EXPORT_NAME="WASM"

FPZIP_FP = FPZIP_FP_FAST
FPZIP_BLOCK_SIZE = 0x1000
DEFS += -DFPZIP_BLOCK_SIZE=$(FPZIP_BLOCK_SIZE) -DFPZIP_FP=$(FPZIP_FP) $(FPZIP_CONV)

FFMPEG_FLAGS = -Wno-implicit-function-declaration -DARCH_X86=0 -DHAVE_FAST_UNALIGNED=0 -DFF_MEMORY_POISON=0x2a -s ERROR_ON_UNDEFINED_SYMBOLS=0 -I./FFmpeg -I./FFmpeg/libavutil
FFMPEG_SRC = FFmpeg/libavutil/mastering_display_metadata.c FFmpeg/libavutil/dict.c FFmpeg/libavutil/display.c FFmpeg/libavutil/frame.c FFmpeg/libavutil/channel_layout.c FFmpeg/libavutil/samplefmt.c FFmpeg/libavutil/avstring.c FFmpeg/libavutil/md5.c FFmpeg/libavutil/rational.c FFmpeg/libavutil/mathematics.c FFmpeg/libavutil/opt.c FFmpeg/libavutil/eval.c FFmpeg/libavutil/time.c FFmpeg/libavutil/parseutils.c FFmpeg/libavutil/random_seed.c FFmpeg/libavutil/sha.c FFmpeg/libavutil/stereo3d.c FFmpeg/libavutil/hwcontext.c FFmpeg/libavutil/error.c FFmpeg/libavutil/file_open.c FFmpeg/libavutil/reverse.c FFmpeg/libavcodec/parser.c FFmpeg/libavcodec/parsers.c FFmpeg/libavcodec/bswapdsp.c FFmpeg/libavcodec/avpacket.c FFmpeg/libavcodec/options.c FFmpeg/libavcodec/allcodecs.c FFmpeg/libavcodec/codec_desc.c FFmpeg/libavcodec/decode.c FFmpeg/libavcodec/bsf.c FFmpeg/libavcodec/bitstream_filters.c FFmpeg/libavcodec/hevc_refs.c FFmpeg/libavcodec/hevcdec.c FFmpeg/libavcodec/hevc_cabac.c FFmpeg/libavcodec/hevc_filter.c FFmpeg/libavcodec/hevcdsp.c FFmpeg/libavcodec/hevc_mvs.c FFmpeg/libavcodec/hevcpred.c FFmpeg/libavcodec/cabac.c FFmpeg/libavcodec/videodsp.c FFmpeg/libavcodec/profiles.c FFmpeg/libavcodec/null_bsf.c FFmpeg/libavcodec/hevc_parse.c FFmpeg/libavcodec/hevc_parser.c FFmpeg/libavcodec/hevc_ps.c FFmpeg/libavutil/buffer.c FFmpeg/libavutil/pixdesc.c FFmpeg/libavutil/mem.c FFmpeg/libavutil/imgutils.c FFmpeg/libavutil/log.c FFmpeg/libavutil/bprint.c FFmpeg/libavutil/intmath.c FFmpeg/libavutil/log2_tab.c FFmpeg/libavcodec/h2645_parse.c FFmpeg/libavcodec/utils.c FFmpeg/libavcodec/hevc_sei.c FFmpeg/libavcodec/golomb.c FFmpeg/libavcodec/hevc_data.c hevc_decoder.c
FFMPEG_OBJ = $(FFMPEG_SRC:.c=.o)

%.o: %.c
	emcc $(FFMPEG_FLAGS) -o $@ -c $<

wasm: $(FFMPEG_OBJ)
	em++ $(EMFLAGS) $(DEFS) $(SRC) colourmap.cc main.cc -o ../htdocs/fitswebql/exr.$(VERSION).js --post-js module-post.js $(INCLUDE) -L$(LIBRARY) $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f ../htdocs/fitswebql/exr.*.js ../htdocs/fitswebql/exr.*.js.mem ../htdocs/fitswebql/exr.*.wasm

clean2:
	rm -f $(FFMPEG_OBJ)