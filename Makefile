
CC=			gcc
VPATH=src:obj:lib
OBJ_DIR=./obj/
LIB_DIR=./lib/
#SHD_DIR=./src/shd_filter/
#CC=			clang --analyze
CFLAGS=		-g -Wall -Wno-unused-function -O2 -msse4.2 
WRAP_MALLOC=-DUSE_MALLOC_WRAPPERS
AR=			ar
DFLAGS=		-DHAVE_PTHREAD $(WRAP_MALLOC)

LOBJS=		utils.o kthread.o kstring.o ksw.o bwt.o bntseq.o bwa.o bwamem.o bwamem_pair.o bwamem_extra.o malloc_wrap.o \
			QSufSort.o bwt_gen.o rope.o rle.o is.o bwtindex.o 
LOBJS_PATH=$(addprefix $(OBJ_DIR),$(LOBJS))
SHD_OBJS=mask.o print.o bit_convert.o popcount.o vector_filter.o
SHD_OBJS_PATH=$(addprefix $(OBJ_DIR),$(SHD_OBJS))
#SHD_SRC_PATH=$(addprefix $(SHD_DIR),$(SHD_OBJS))
AOBJS=		bwashm.o bwase.o bwaseqio.o bwtgap.o bwtaln.o bamlite.o \
			bwape.o kopen.o pemerge.o maxk.o \
			bwtsw2_core.o bwtsw2_main.o bwtsw2_aux.o bwt_lite.o \
			bwtsw2_chain.o fastmap.o bwtsw2_pair.o
AOBJS_PATH=$(addprefix $(OBJ_DIR),$(AOBJS))
PROG=		gase
INCLUDES=	
LIBS=		-lm -lz -lpthread
SUBDIRS=	.

ifeq ($(shell uname -s),Linux)
	LIBS += -lrt
endif

.SUFFIXES:.c .o .cc .cpp

.c.o:
		$(CC) -c $(CFLAGS) $(DFLAGS) $(INCLUDES) $< -o $(OBJ_DIR)$@

.cpp.o:
		g++ -c $(CFLAGS) $(INCLUDES) $< -o $(OBJ_DIR)$(notdir $@)


all: makedir $(PROG) 

makedir:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(LIB_DIR)
	@echo "If you donot see anything below this line then there is nothing to \"make\""

gase:libbwa.a libshd_filter.a $(AOBJS) main.o
		$(CC) $(CFLAGS) $(DFLAGS) $(AOBJS_PATH) $(OBJ_DIR)main.o -o $@ -L$(LIB_DIR) -lbwa -lshd_filter $(LIBS)


libbwa.a:$(LOBJS)
		$(AR) -csru $(LIB_DIR)$@ $(LOBJS_PATH)

libshd_filter.a: $(SHD_OBJS)
		#make -C ./src/shd_filter libshd_filter.a
		ar -csru $(LIB_DIR)$@ $(SHD_OBJS_PATH) 		

clean:
		rm -f -r gmon.out $(OBJ_DIR) a.out $(PROG) *~ $(LIB_DIR)
		#make -C ./src/shd_filter/ clean

depend:
	( LC_ALL=C ; export LC_ALL; cd src; makedepend -Y -- $(CFLAGS) $(DFLAGS) -- -f ../Makefile -p $(OBJ_DIR)  *.c *.cpp )

# DO NOT DELETE THIS LINE -- make depend depends on it.

./obj/QSufSort.o: QSufSort.h
./obj/bamlite.o: bamlite.h malloc_wrap.h
./obj/bntseq.o: bntseq.h utils.h kseq.h malloc_wrap.h khash.h
./obj/bwa.o: bntseq.h bwa.h bwt.h ksw.h utils.h kstring.h malloc_wrap.h
./obj/bwa.o: kvec.h kseq.h
./obj/bwamem.o: kstring.h malloc_wrap.h bwamem.h bwt.h bntseq.h bwa.h ksw.h
./obj/bwamem.o: kvec.h ksort.h utils.h kbtree.h
./obj/bwamem_extra.o: bwa.h bntseq.h bwt.h bwamem.h kstring.h malloc_wrap.h
./obj/bwamem_pair.o: kstring.h malloc_wrap.h bwamem.h bwt.h bntseq.h bwa.h
./obj/bwamem_pair.o: kvec.h utils.h ksw.h
./obj/bwape.o: bwtaln.h bwt.h kvec.h malloc_wrap.h bntseq.h utils.h bwase.h
./obj/bwape.o: bwa.h ksw.h khash.h
./obj/bwase.o: bwase.h bntseq.h bwt.h bwtaln.h utils.h kstring.h
./obj/bwase.o: malloc_wrap.h bwa.h ksw.h
./obj/bwaseqio.o: bwtaln.h bwt.h utils.h bamlite.h malloc_wrap.h kseq.h
./obj/bwashm.o: bwa.h bntseq.h bwt.h
./obj/bwt.o: utils.h bwt.h kvec.h malloc_wrap.h
./obj/bwt_gen.o: QSufSort.h malloc_wrap.h
./obj/bwt_lite.o: bwt_lite.h malloc_wrap.h
./obj/bwtaln.o: bwtaln.h bwt.h bwtgap.h utils.h bwa.h bntseq.h malloc_wrap.h
./obj/bwtgap.o: bwtgap.h bwt.h bwtaln.h malloc_wrap.h
./obj/bwtindex.o: bntseq.h bwa.h bwt.h utils.h rle.h rope.h malloc_wrap.h
./obj/bwtsw2_aux.o: bntseq.h bwt_lite.h utils.h bwtsw2.h bwt.h kstring.h
./obj/bwtsw2_aux.o: malloc_wrap.h bwa.h ksw.h kseq.h ksort.h
./obj/bwtsw2_chain.o: bwtsw2.h bntseq.h bwt_lite.h bwt.h malloc_wrap.h
./obj/bwtsw2_chain.o: ksort.h
./obj/bwtsw2_core.o: bwt_lite.h bwtsw2.h bntseq.h bwt.h kvec.h malloc_wrap.h
./obj/bwtsw2_core.o: khash.h ksort.h
./obj/bwtsw2_main.o: bwt.h bwtsw2.h bntseq.h bwt_lite.h utils.h bwa.h
./obj/bwtsw2_pair.o: utils.h bwt.h bntseq.h bwtsw2.h bwt_lite.h kstring.h
./obj/bwtsw2_pair.o: malloc_wrap.h ksw.h
./obj/example.o: bwamem.h bwt.h bntseq.h bwa.h kseq.h malloc_wrap.h
./obj/fastmap.o: bwa.h bntseq.h bwt.h bwamem.h kvec.h malloc_wrap.h utils.h
./obj/fastmap.o: kseq.h
./obj/is.o: malloc_wrap.h
./obj/kopen.o: malloc_wrap.h
./obj/kstring.o: kstring.h malloc_wrap.h
./obj/ksw.o: ksw.h malloc_wrap.h
./obj/main.o: kstring.h malloc_wrap.h utils.h
./obj/malloc_wrap.o: malloc_wrap.h
./obj/maxk.o: bwa.h bntseq.h bwt.h bwamem.h kseq.h malloc_wrap.h
./obj/pemerge.o: ksw.h kseq.h malloc_wrap.h kstring.h bwa.h bntseq.h bwt.h
./obj/pemerge.o: utils.h
./obj/rle.o: rle.h
./obj/rope.o: rle.h rope.h
./obj/utils.o: utils.h ksort.h malloc_wrap.h kseq.h
./obj/bit_convert.o: print.h bit_convert.h
./obj/bit_convertMain.o: bit_convert.h
./obj/countPassFilter.o: vector_filter.h mask.h
./obj/mask.o: mask.h
./obj/popcount.o: popcount.h mask.h
./obj/popcountMain.o: popcount.h
./obj/print.o: print.h
./obj/read_modifier.o: read_modifier.h
./obj/shiftMain.o: vector_filter.h mask.h
./obj/string_cp.o: print.h
./obj/test_modifier.o: read_modifier.h vector_filter.h
./obj/vector_filter.o: print.h vector_filter.h popcount.h bit_convert.h
./obj/vector_filter.o: mask.h
./obj/vector_filterMain.o: vector_filter.h mask.h
