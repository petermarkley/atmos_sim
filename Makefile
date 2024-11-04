CFLAGS=-g -Wall
DFLAGS=
PFLAGS=-Imd5/ -Istupid/ -I/opt/local/include/
LFLAGS=-lm -lSDL2 -lSDL2_image -L/opt/local/lib/
MODULES=

all: output

atmos_sim: atmos_sim.c
	cc -o atmos_sim atmos_sim.c $(MODULES) $(CFLAGS) $(PFLAGS) $(LFLAGS)

frames: atmos_sim
	./atmos_sim
	touch frames

frames-edit: frames
	mkdir -p frames-edit
	./img_edit.sh
	touch frames-edit

output: frames-edit
	mkdir -p output
	ffmpeg -r 5 -pattern_type glob -i "frames-edit/*.png" -vf "scale=2211:1062" output/turbulence.mp4
	ffmpeg -r 5 -pattern_type glob -i "frames-edit/*.png" -vf "scale=2211:1062" output/turbulence.gif
	touch output

clean:
	rm -rf atmos_sim atmos_sim.dSYM frames* output
