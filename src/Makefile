PROG1=modesd

PROG1MODS=modesd microadsb udp util
PROG1SRCS=$(addsuffix .c,$(PROG1MODS))
PROG1OBJS=$(addsuffix .o,$(PROG1MODS))
PROG1CLEAN=$(PROG1) $(PROG1OBJS)

$(PROG1): $(PROG1OBJS)
	$(CC) -o $(PROG1) $(PROG1OBJS)

modesd.o: udp.h util.h microadsb.h
microadsb.o: microadsb.h
udp.o: udp.h
util.o: util.h

clean:
	$(RM) $(PROG1CLEAN)
