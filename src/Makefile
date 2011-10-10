include make.local

LIBMODS=util udp microadsb
LIBMODHDR=$(addsuffix .h,$(LIBMODS))

PROG1=modesd
PROG2=nbmodes
PROGS=$(PROG1) $(PROG2)

all: $(PROGS)

##

PROG1MODS=$(PROG1) $(LIBMODS)
PROG1SRCS=$(addsuffix .c,$(PROG1MODS))
PROG1OBJS=$(addsuffix .o,$(PROG1MODS))
PROG1CLEAN=$(PROG1) $(PROG1OBJS)

$(PROG1): $(PROG1OBJS)
	$(CC) -o $(PROG1) $(PROG1OBJS)

$(PROG1).o: $(LIBMODHDR)

##

PROG2MODS=$(PROG2) $(LIBMODS)
PROG2SRCS=$(addsuffix .c,$(PROG2MODS))
PROG2OBJS=$(addsuffix .o,$(PROG2MODS))
PROG2CLEAN=$(PROG2) $(PROG2OBJS)

X11LIBS=$(X11LIBDIR) -lXt -lX11

$(PROG2): $(PROG2OBJS)
	$(CC) -o $(PROG2) $(PROG2OBJS) $(X11LIBS)

$(PROG2).o: $(LIBMODHDR)

##

clean:
	$(RM) $(PROG1CLEAN) $(PROG2CLEAN)

microadsb.o: microadsb.h
udp.o: udp.h
util.o: util.h

make.local:
	touch make.local
