MOD=modesd

CFILES=modesd udp util
SRCS=$(addsuffix .c,$(CFILES))
OBJS=$(addsuffix .o,$(CFILES))

modesd: $(OBJS)
	$(CC) -o $(MOD) $(OBJS)

modesd.o: udp.h util.h
udp.o: udp.h
util.o: util.h
