OBJS=omxiv.o
BIN=omxiv.bin
LDFLAGS+=-lilclient -lomxlib -ljpeg -lpng -lrt -ldl -Wl,--gc-sections -s
INCLUDES+=-I./libs/ilclient -I./libs/omxlib

LIBCURL_NAME=\"$(shell ldconfig -p | grep libcurl | head -n 1 | awk '{print $$1;}' 2>/dev/null)\"
CFLAGS+=-DLCURL_NAME=$(LIBCURL_NAME)

include Makefile.include

$(BIN): help.h $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
	
clean:: 
	@rm -f help.h
	
help.h: README.md
	echo -n "static void printUsage(){printf(\"" > help.h
	sed -n -e '/\#\# Synopsis/,/\#\# / p' README.md | sed -e '1d;$$d' | sed ':a;N;$$!ba;s/\n/\\n/g' | tr '\n' ' ' >> help.h
	echo "\\\\n\");}" >> help.h

ilclient:
	cp -ru /opt/vc/src/hello_pi/libs/ilclient libs
	make -C libs/ilclient

omxlibs:
	make -C libs/omxlib

install:
	install $(BIN) /usr/bin/omxiv

uninstall:
	rm -f /usr/bin/omxiv
