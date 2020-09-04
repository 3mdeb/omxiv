OBJS=omxiv.o omx_image.o omx_render.o soft_image.o
BIN=omxiv.bin
LDFLAGS+=-lilclient -ljpeg -lpng -lrt -ldl -Wl,--gc-sections -s
INCLUDES+=-I./libs/ilclient -I./libs/omxlib

LIBCURL_NAME=\"$(shell ldconfig -p | grep libcurl | head -n 1 | awk '{print $$1;}' 2>/dev/null)\"
CFLAGS+=-DLCURL_NAME=$(LIBCURL_NAME)

ilclient:
	cp -ru /opt/vc/src/hello_pi/libs/ilclient libs
	make -C libs/ilclient

libs:
	make -C libs/omxlib
