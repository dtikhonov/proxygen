SOURCES=CompressionSimulator.cpp Main.cpp ../qpack/QPACKCodec.cpp \
	../qpack/QPACKContext.cpp ../qpack/QPACKDecoder.cpp \
	../qpack/QPACKEncoder.cpp ../qpack/QPACKHeaderTable.cpp \
	../../test/HTTPArchive.cpp

HEADERS=CompressionScheme.h CompressionSimulator.h HPACKScheme.h \
	QCRAMScheme.h SimStreamingCallback.h QMINScheme.h \
	CompressionTypes.h QPACKScheme.h ../qpack/QPACKCodec.h \
	../qpack/QPACKContext.h ../qpack/QPACKDecoder.h ../qpack/QPACKEncoder.h \
	../qpack/QPACKHeader.h ../qpack/QPACKHeaderTable.h \
	../../test/HTTPArchive.h qmin/qmin_enc.h

simulator: ${SOURCES} ${HEADERS}
	g++ -std=gnu++14 -o $@ -I ../../../../../../.. -I ../qpack ${SOURCES} -L qmin -lqmin -lfolly -lfollylogging -lfollyinit -lglog -lgflags -lproxygenlib -lpthread -ldouble-conversion
