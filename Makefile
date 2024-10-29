CFLAGS=-g -Wall
DFLAGS=
PFLAGS=-Imd5/ -Istupid/ -I/opt/local/include/
LFLAGS=-lm -lSDL2 -lSDL2_image -L/opt/local/lib/
MODULES=

all: atmos_sim.c
	cc -o atmos_sim atmos_sim.c $(MODULES) $(CFLAGS) $(PFLAGS) $(LFLAGS)

clean:
	rm -rf atmos_sim atmos_sim.dSYM
