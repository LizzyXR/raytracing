CC = cc
CFLAGS = -std=c99 -O2
LDFLAGS = -lm -lpthread

all: raytracing

raytracing: raytracing.c
	$(CC) $(CFLAGS) -o raytracing raytracing.c $(LDFLAGS)

clean:
	rm -f raytracing

.PHONY: all
