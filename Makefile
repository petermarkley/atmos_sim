CFLAGS=-g -Wall
DFLAGS=
PFLAGS=-Imd5/ -Istupid/ -I/opt/local/include/
LFLAGS=-lm -lSDL2 -lSDL2_image -L/opt/local/lib/
MODULES=

all: out.mp4

atmos_sim: atmos_sim.c
	cc -o atmos_sim atmos_sim.c $(MODULES) $(CFLAGS) $(PFLAGS) $(LFLAGS)

frames: atmos_sim
	./atmos_sim

out.mp4: frames
	mkdir -p output
	ffmpeg -r 5 -pattern_type glob -i "frames/*.png" -vf "scale=2011:191" output/out.mp4

clean:
	rm -rf atmos_sim atmos_sim.dSYM frames output
