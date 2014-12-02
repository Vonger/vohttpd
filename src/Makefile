CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc
INCLUDES = 
CFLAGS = 

CCLD = $(CC)
LIBS = -ldl
LDFLAGS = 

PROGRAM = vohttpd
OBJ = vohttpd.o vohttpdext.o

PLUGINS = $(patsubst %.c,%.so,$(wildcard plugins/*.c))
PLUGINS_C = vohttpdext.c
PLUGINS_CFLAGS = -fPIC -shared 

all: $(PROGRAM)


%.o: %.c
	@$(CC) -c $< $(INCLUDES) $(CFLAGS)
	@echo "	CC	"$@

$(PROGRAM): $(OBJ)
	@$(CCLD) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)
	@echo "	CCLD	"$@

$(PLUGINS): %.so:%.c $(PLUGINS_C)
	@$(CCLD) -o $@ $(PLUGINS_CFLAGS) $^
	@echo "	CCLD	"$@

.SUFFIXES: all clean plugins

plugins: $(PLUGINS)

clean:
	-rm -f *.o
	-rm -f $(PROGRAM)
	-rm -f $(PLUGINS)
