#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "SDL2/SDL_image.h"
#include "SDL2/SDL.h"
#include "spb.h"
#include "vector3D.h"

/*
 |  ========================
 |  COMPUTATIONAL PARAMETERS
 |  ========================
 */
#define MAX_STR 1024
#define ATMOS_STOP_NUM 100

/*
 |  =====================
 |  SIMULATION PARAMETERS
 |  =====================
 */
#define WINDOW_ARC_LENGTH 400.0 // kilometers
#define WINDOW_ALTITUDE 35.0 // kilometers
#define IMAGE_RES 10.0 // pixels per kilometer

#define FRAME_FOLDER "frames"
#define FRAMES 50
#define BLOOPS_PER_FRAME 10
#define CONTOUR_NUM 18 // number of contour lines on density map
#define DENSITY_MAX 1.8 // top of heat map color ramp, in kg/m^3

/*
 |  ==================
 |  PHYSICAL CONSTANTS
 |  ==================
 */
#define EARTH_RADIUS 6371.0 // kilometers
#define EARTH_CIRCUMFERENCE 40030.173592041 // kilometers
/*
 |  The Gladstone-Dale constant for air, at 273 Kelvin and 14.7 psi,
 |  for light in the visible spectrum. See Fig. 3(a) here:
 |  - https://pubs.aip.org/aip/pof/article/35/8/086121/2906851/High-temperature-and-pressure-Gladstone-Dale
 */
#define GLADSTONEDALE_CONST 2.3e-4 // m^3/kg

/*
 |  =================
 |  RENDERING METRICS
 |  =================
 */
//let's caculate some global variables based on the simulation parameters
double WINDOW_ANGLE, WINDOW_TOP, WINDOW_RIGHT, WINDOW_LEFT, WINDOW_BOTTOM;
int IMAGE_WIDTH, IMAGE_HEIGHT, BLOOP_NUM;
void global_init() {
  WINDOW_ANGLE = (WINDOW_ARC_LENGTH/EARTH_CIRCUMFERENCE) * 360.0; // degrees
  WINDOW_TOP = EARTH_RADIUS + WINDOW_ALTITUDE; // kilometers
  WINDOW_RIGHT = sin((WINDOW_ANGLE/2.0) * (PI/180.0)) * WINDOW_TOP; // kilometers
  WINDOW_LEFT = -WINDOW_RIGHT; // kilometers
  WINDOW_BOTTOM = cos((WINDOW_ANGLE/2.0) * (PI/180.0)) * EARTH_RADIUS; // kilometers
  IMAGE_WIDTH = (int)ceil( (WINDOW_RIGHT-WINDOW_LEFT) * IMAGE_RES ); // pixels
  IMAGE_HEIGHT = (int)ceil( (WINDOW_TOP-WINDOW_BOTTOM) * IMAGE_RES ); // pixels
  BLOOP_NUM = FRAMES * BLOOPS_PER_FRAME;
}

/*
 |  ============
 |  DATA OBJECTS
 |  ============
 */
//for passing data to our SDL_Image subroutine
struct pixel {
  double r, g, b;
};
//globe-centric polar coordinate
struct atmos_coord {
  double alt; // altitude in kilometers
  double ground; // ground point in kilometers
};
//this is used for approximating the baseline density gradient
struct atmos_grade_stop {
  double alt; // altitude in kilometers
  double density; // density in kg/m^3
} atmos_grade[ATMOS_STOP_NUM];
//discrete primitives for simulating turbulence
struct atmos_bloop {
  double x, y; //window coordinate
  double t; //coordinate within life span (between 0 and 1)
  double startx, starty, endx, endy; //motion endpoints
  double startt, dur; //start time and duration
  struct atmos_coord coord; //center of bloop
  double radv, radh; //polar radii, vertical & horizontal
  double amp; //peak amplitude (at centerpoint, midway through duration)
} *bloop_list;
//density field contour lines
struct atmos_contour {
  double density; // kg/m^3
} *contour_list;
//this is used for the heat map color ramp
struct grade_stop {
  double val;
  struct pixel color;
};
//used for sight line raytracing
struct ray_node {
  long double x, y;
};
struct atmos_ray {
  struct ray_node *nodes;
  int buffsize; //memory buffer size
  int num; //actual number used so far
  struct vectorC3D dir; //direction vector at ray endpoint
  struct ray_node *start; //this should point to the beginning node
  struct ray_node *end; //this should point to the end node
} sight;

//atmspheric density field, in kg/m^3
double **atmos;

/*
 |  ===================
 |  LOW-LEVEL UTILITIES
 |  ===================
 */

//generate a random floating point between 0 and 1
long double rng(void) {
  return ((long double)rand())/((long double)RAND_MAX);
}
/*
 |  We're using this for approximating a standard atmospheric
 |  density gradient. Math is dimension-agnostic; function is
 |  called once for each axis (X and Y). See also:
 |  - https://en.wikipedia.org/wiki/B%C3%A9zier_curve#Cubic_B%C3%A9zier_curves
 */
double bezier_cubic(double n1, double h1, double h2, double n2, double frac) {
  double q1, q2, q3;
  double r1, r2;
  q1 = (h1-n1)*frac+n1;
  q2 = (h2-h1)*frac+h1;
  q3 = (n2-h2)*frac+h2;
  r1 = (q2-q1)*frac+q1;
  r2 = (q3-q2)*frac+q2;
  return (r2-r1)*frac+r1;
}
//map a pixel object to an SDL image object
void pixel_insert(struct SDL_Surface *s, struct pixel p, int x, int y) {
  ((Uint8 *)s->pixels)[y*s->pitch+x*3+0] = (Uint8)(255.0*fmax(0,fmin(1,p.b)));
  ((Uint8 *)s->pixels)[y*s->pitch+x*3+1] = (Uint8)(255.0*fmax(0,fmin(1,p.g)));
  ((Uint8 *)s->pixels)[y*s->pitch+x*3+2] = (Uint8)(255.0*fmax(0,fmin(1,p.r)));
}
//calculate color ramp for density heat map
void density_to_color(struct pixel *pix, double density, int x, int y) {
  int stop_num = 5;
  struct grade_stop grade[stop_num], *floor, *ceil;
  int i;
  double frac;
  /*
   | This color ramp data is a bit hard-wired, but hey it works.
   */
  grade[0].val = 0.0;
  grade[0].color.r = 0.05; grade[0].color.g = 0.05; grade[0].color.b = 0.05;
  grade[1].val = (1.0/(stop_num-1))*DENSITY_MAX;
  grade[1].color.r = 0.00; grade[1].color.g = 0.00; grade[1].color.b = 0.20;
  grade[2].val = (2.0/(stop_num-1))*DENSITY_MAX;
  grade[2].color.r = 0.00; grade[2].color.g = 0.18; grade[2].color.b = 0.20;
  grade[3].val = (3.0/(stop_num-1))*DENSITY_MAX;
  grade[3].color.r = 0.20; grade[3].color.g = 0.20; grade[3].color.b = 0.00;
  grade[4].val = (4.0/(stop_num-1))*DENSITY_MAX;
  grade[4].color.r = 0.20; grade[4].color.g = 0.04; grade[4].color.b = 0.00;
  for (i=0; i < stop_num; i++) {
    floor = &(grade[i]);
    if (i == (stop_num-1)) {
      ceil = &(grade[i]);
    } else {
      ceil = &(grade[i+1]);
    }
    if (density >= floor->val && density <= ceil->val) {
      frac = (density - floor->val)/(ceil->val - floor->val);
      pix->r = (ceil->color.r-floor->color.r)*frac + floor->color.r;
      pix->g = (ceil->color.g-floor->color.g)*frac + floor->color.g;
      pix->b = (ceil->color.b-floor->color.b)*frac + floor->color.b;
      return;
    }
  }
  //density out of range, print warning color
  pix->r = 1.0; pix->g = 0.0; pix->b = 1.0;
  return;
}

/*
 |  ======================
 |  RENDER SPACE UTILITIES
 |  ======================
 */

//calculate altitude & ground point of the given window point
void atmos_coords(double x, double y, struct atmos_coord *coord) {
  struct vectorC3D c;
  struct vectorP3D p;
  c.x = (x/IMAGE_WIDTH)*(WINDOW_RIGHT-WINDOW_LEFT) + WINDOW_LEFT;
  c.y = 0.0;
  c.z = ((IMAGE_HEIGHT-y)/IMAGE_HEIGHT)*(WINDOW_TOP-WINDOW_BOTTOM) + WINDOW_BOTTOM;
  vectorP3D_assign(&p,vectorC3D_polar(c));
  coord->alt = p.l - EARTH_RADIUS;
  coord->ground = ( (90.0 - p.y + (WINDOW_ANGLE/2.0)) / WINDOW_ANGLE ) * WINDOW_ARC_LENGTH;
  return;
}
//calculate window point from altitude & ground point
void atmos_window(double *x, double *y, struct atmos_coord *coord) {
  struct vectorC3D c;
  struct vectorP3D p;
  p.l = coord->alt + EARTH_RADIUS;
  p.x = 0.0;
  p.y = 90.0 - ( (coord->ground/WINDOW_ARC_LENGTH) * WINDOW_ANGLE ) + (WINDOW_ANGLE/2.0);
  vectorC3D_assign(&c,vectorP3D_cartesian(p));
  x[0] = ((c.x-WINDOW_LEFT)/(WINDOW_RIGHT-WINDOW_LEFT)) * IMAGE_WIDTH;
  y[0] = (1.0 - (c.z-WINDOW_BOTTOM)/(WINDOW_TOP-WINDOW_BOTTOM)) * IMAGE_HEIGHT;
  return;
}
//interpolate values for fractional window coordinates
double atmos_val(double x, double y) {
  double tl, tr, bl, br;
  int top, left, bottom, right;
  double fracx, fracy;
  double wtl, wtr, wbl, wbr;
  double final;
  //sanity check
  if (x < 0.5 || x > IMAGE_WIDTH-0.5 || y < 0.5 || y > IMAGE_WIDTH-0.5) {
    return 0.0;
  }
  //check for lucky cases when we can skip the fancy math
  if (x == (double)((int)x) && y == (double)((int)y)) {
    return atmos[(int)y][(int)x];
  }
  
  //okay, gotta do the work ...
  
  //safe array indices
  top = MAX(0,MIN((IMAGE_HEIGHT-1), (int)floor(y) ));
  left = MAX(0,MIN((IMAGE_WIDTH-1), (int)floor(x) ));
  bottom = MAX(0,MIN((IMAGE_HEIGHT-1), (int)ceil(y) ));
  right = MAX(0,MIN((IMAGE_WIDTH-1), (int)ceil(x) ));
  //values for corners of fractional region
  tl = atmos[top][left];
  tr = atmos[top][right];
  bl = atmos[bottom][left];
  br = atmos[bottom][right];
  //components of position in fractional region
  fracx = x-floor(x);
  fracy = y-floor(y);
  //weights for each corner
  wtl = (1.0-fracx)*(1.0-fracy);
  wtr = fracx*(1.0-fracy);
  wbl = (1.0-fracx)*fracy;
  wbr = fracx*fracy;
  //final
  final = tl*wtl + tr*wtr + bl*wbl + br*wbr;
  return final;
}
//are we in bounds?
int atmos_bounds(double x, double y) {
  struct atmos_coord coord;
  atmos_coords(x,y,&coord);
  if (coord.ground < 0.0 || coord.ground > WINDOW_ARC_LENGTH ||
      coord.alt < 0.0 || coord.alt > WINDOW_ALTITUDE) {
    return 0;
  }
  return 1;
}

/*
 |  =====================
 |  SIMULATING TURBULENCE
 |  =====================
 */

//generate random bloops
int bloop_init() {
  struct atmos_bloop *bloop;
  int i;
  if ((bloop_list = (struct atmos_bloop *)calloc(sizeof(struct atmos_bloop), BLOOP_NUM)) == NULL) {
    fprintf(stderr, "calloc(): %s\n", strerror(errno));
    return -1;
  }
  for (i=0; i < BLOOP_NUM; i++) {
    bloop = &(bloop_list[i]);
    bloop->startx = rng()*IMAGE_WIDTH;
    bloop->starty = rng()*IMAGE_HEIGHT;
    bloop->endx = bloop->startx + rng()*IMAGE_WIDTH*0.02;
    bloop->endy = bloop->starty + rng()*IMAGE_HEIGHT*0.02;
    bloop->dur = rng()*FRAMES*1.2;
    bloop->startt = rng()*FRAMES - bloop->dur/2.0;
    bloop->radv = rng()*4.0+1.0;
    bloop->radh = rng()*40.0+10.0;
    bloop->amp = pow(2.0, rng()*0.6-0.3 );
  }
  return 0;
}
//set dynamic variables for this bloop at the given time coordinate 
void bloop_cycle(double t, struct atmos_bloop *bloop) {
  //how far are we through this bloop's life span?
  bloop->t = (t - bloop->startt) / (bloop->dur);
  //set center point at proper location along motion path
  bloop->x = (bloop->endx - bloop->startx) * bloop->t + bloop->startx;
  bloop->y = (bloop->endy - bloop->starty) * bloop->t + bloop->starty;
  //calculate altitude & ground position of center point
  atmos_coords(bloop->x,bloop->y,&(bloop->coord));
  return;
}
//calculate the multiplier that this bloop applies to the density field at the given sample point
double bloop_calc(double x, double y, double t, struct atmos_bloop *bloop) {
  struct atmos_coord sample;
  double sv, sh, ratio;
  double dist, amp, val;
  //sanity check
  if (bloop->t <= 0.0 && bloop->t >= 1.0) {
    return 1.0;
  }
  //find sample in bloop-centered coordinate space
  atmos_coords(x,y,&sample);
  sh = sample.ground - bloop->coord.ground;
  sv = sample.alt - bloop->coord.alt;
  //transform coordinate space into circle
  ratio = bloop->radh / bloop->radv;
  sv = sv*ratio;
  //calculate distance from center
  dist = sqrt(pow(sv,2.0)+pow(sh,2.0));
  if (dist > bloop->radh) {
    return 1.0;
  }
  //calculate current amplitude of bloop
  amp = (0.5 - cos(bloop->t*PI*2.0)*0.5) * (bloop->amp-1.0) + 1.0;
  //calculate final value
  val = (cos((dist/bloop->radh)*PI)*0.5+0.5) * (amp-1.0) + 1.0;
  return val;
}
//apply the bloop to the density field
void bloop_apply(double t, struct atmos_bloop *bloop) {
  int x, y;
  int min_x, min_y, max_x, max_y;
  bloop_cycle(t,bloop);
  //sanity check
  if (bloop->t <= 0.0 && bloop->t >= 1.0) {
    return;
  }
  //calculate a bounding box
  min_x = MAX(0, (int)(bloop->x - bloop->radh*IMAGE_RES));
  max_x = MIN(IMAGE_WIDTH-1, (int)(bloop->x + bloop->radh*IMAGE_RES));
  min_y = MAX(0, (int)(bloop->y - bloop->radv*IMAGE_RES));
  max_y = MIN(IMAGE_HEIGHT-1, (int)(bloop->y + bloop->radv*IMAGE_RES));
  //loop through pixels inside bounding box
  for (y=min_y; y <= max_y; y++) {
    for (x=min_x; x <= max_x; x++) {
      atmos[y][x] = atmos[y][x] * bloop_calc(x,y,t,bloop);
    }
  }
  return;
}

/*
 |  =============================
 |  CONTOUR LINES FOR DENSITY MAP
 |  =============================
 */

//initialize contour list
int contour_init() {
  struct atmos_contour *contour;
  int i;
  double interval = ((double)DENSITY_MAX) / ((double)CONTOUR_NUM);
  double density;
  if ((contour_list = (struct atmos_contour *)calloc(sizeof(struct atmos_contour), CONTOUR_NUM)) == NULL) {
    fprintf(stderr, "calloc(): %s\n", strerror(errno));
    return -1;
  }
  for (i=0; i < CONTOUR_NUM; i++) {
    density = ((double)(i+1)) * interval;
    contour = &(contour_list[i]);
    contour->density = density;
  }
  return 0;
}
//return 1 if given pixel is on a contour line, otherwise 0
int contour_detect(int x, int y) {
  struct atmos_contour *contour;
  double tl, tr, bl, br;
  int i, itl, itr, ibl, ibr;
  double prev, curr;
  /*
   |  take a sample at each corner of this pixel, halfway between
   |  it and the neighboring pixels
   */
  tl = atmos_val(((double)x)-0.5,((double)y)-0.5);
  tr = atmos_val(((double)x)+0.5,((double)y)-0.5);
  bl = atmos_val(((double)x)-0.5,((double)y)+0.5);
  br = atmos_val(((double)x)+0.5,((double)y)+0.5);
  //corner indices in contour array will be negative until initialized
  itl = itr = ibl = ibr = -1;
  //find corner indices
  curr = -1.0;
  for (i=0; i < CONTOUR_NUM; i++) {
    contour = &(contour_list[i]);
    prev = curr;
    curr = contour->density;
    if (tl > prev && tl <= curr) {
      itl = i;
    }
    if (tr > prev && tr <= curr) {
      itr = i;
    }
    if (bl > prev && bl <= curr) {
      ibl = i;
    }
    if (br > prev && br <= curr) {
      ibr = i;
    }
    //are we done yet?
    if (itl >= 0 && itr >= 0 && ibl >= 0 && ibr >= 0) {
      break;
    }
  }
  /*
   |  if all four corners lie within the same density interval, then
   |  no contour line runs through this pixel
   */
  if (
    (itl == itr)
    && (ibl == ibr)
    && (itl == ibl)
    //&& (itr == ibr) //this last comparison can be inferred
  ) {
    return 0;
  } else {
    return 1;
  }
}

/*
 |  ================
 |  ATMOSPHERE LOGIC
 |  ================
 */

//calculate standard density gradient for the given point
double atmos_baseline(double x, double y) {
  struct atmos_coord coord;
  double frac;
  struct atmos_grade_stop *floor, *ceil;
  int i;
  atmos_coords(x,y,&coord);
  
  for (i=0; i < ATMOS_STOP_NUM; i++) {
    floor = &(atmos_grade[i]);
    if (i == (ATMOS_STOP_NUM-1)) {
      ceil = &(atmos_grade[i]);
    } else {
      ceil = &(atmos_grade[i+1]);
    }
    if (coord.alt >= floor->alt && coord.alt <= ceil->alt) {
      frac = (coord.alt - floor->alt)/(ceil->alt - floor->alt);
      return (ceil->density - floor->density)*frac + floor->density;
    }
  }
  //return 1.0 - (coord.alt/WINDOW_ALTITUDE);
  return 0.0;
}
//initialize stuff
int atmos_init() {
  int x, y, i, halfway;
  double n1x, n1y, h1x, h1y, h2x, h2y, n2x, n2y;
  double frac;
  
  //atmospheric density gradient
  /*
   |  This plot relates altitude vs. density of Earth's
   |  atmosphere:
   |  - https://commons.wikimedia.org/wiki/File:Comparison_US_standard_atmosphere_1962.svg
   |  
   |  This is the plot curve in SVG notation, if X axis
   |  is altitude in km and Y axis is density in kg/m^3:
   |  
   |    d="M 0,1.28 C 5,0.60 11,0.31 15,0.20 19,0.08 22,0.02 37,0.00"
   |  
   |  ... or, in other words:
   |  
   |  node: 0,1.28
   |    [handle]: 5,0.60
   |    [handle]: 11,0.31
   |  node: 15,0.20
   |    [handle]: 19,0.08
   |    [handle]: 22,0.02
   |  node: 37,0.00
   */
  halfway = ATMOS_STOP_NUM/2;
  for (i=0; i < ATMOS_STOP_NUM; i++) {
    if (i < halfway) {
      frac = (((double)i) / ((double)(halfway-1)));
      n1x = 00.0; n1y = 1.28;
      h1x = 05.0; h1y = 0.60;
      h2x = 11.0; h2y = 0.31;
      n2x = 15.0; n2y = 0.20;
    } else {
      frac = (((double)(i - halfway)) / ((double)(ATMOS_STOP_NUM-1-halfway)));
      n1x = 15.0; n1y = 0.20;
      h1x = 19.0; h1y = 0.08;
      h2x = 22.0; h2y = 0.02;
      n2x = 37.0; n2y = 0.00;
    }
    atmos_grade[i].alt = bezier_cubic(n1x,h1x,h2x,n2x,frac);
    atmos_grade[i].density = bezier_cubic(n1y,h1y,h2y,n2y,frac);
  }
  
  //atmospheric density field
  if ((atmos = (double **)calloc(sizeof(double *), IMAGE_HEIGHT)) == NULL) {
    fprintf(stderr, "calloc(): %s\n", strerror(errno));
    return -1;
  }
  for (y=0; y < IMAGE_HEIGHT; y++) {
    if ((atmos[y] = (double *)calloc(sizeof(double), IMAGE_WIDTH)) == NULL) {
      fprintf(stderr, "calloc(): %s\n", strerror(errno));
      return -1;
    }
    for (x=0; x < IMAGE_WIDTH; x++) {
      atmos[y][x] = atmos_baseline(x,y);
    }
  }
  
  return 0;
}
//free atmospheric density field
void atmos_free() {
  int y;
  for (y=0; y < IMAGE_HEIGHT; y++) {
    free(atmos[y]);
  }
  free(atmos);
}

/*
 |  ===============
 |  OPTICAL PHYSICS
 |  ===============
 */

/*
 |  This function takes density and returns the absolute refractive
 |  index for air. See:
 |  - https://en.wikipedia.org/wiki/Gladstone%E2%80%93Dale_relation
 |  - https://webmineral.com/help/Gladstone-Dale.shtml
 |  - https://en.wikipedia.org/wiki/Refractive_index
 |  
 |  (See definition of `GLADSTONEDALE_CONST` in this code above for
 |  a citation on that number.)
 */
long double density_to_ior(long double density) {
  return 1.0 + (density * (long double)GLADSTONEDALE_CONST);
}

/*
 |  ==========
 |  RAYTRACING
 |  ==========
 */

//allocate memory buffer
int ray_init() {
  sight.buffsize = 256;
  if ((sight.nodes = (struct ray_node *)calloc(sizeof(struct ray_node), sight.buffsize)) == NULL) {
    fprintf(stderr, "calloc(): %s\n", strerror(errno));
    return -1;
  }
  return 0;
}
//manage potentially growing buffer
int ray_buff() {
  if (sight.num == sight.buffsize) {
    sight.buffsize = sight.buffsize*2;
    if ((sight.nodes = (struct ray_node *)realloc(sight.nodes, sizeof(struct ray_node) * sight.buffsize)) == NULL) {
      fprintf(stderr, "realloc(): %s\n", strerror(errno));
      return -1;
    }
  }
  return 0;
}
//
void ray_walk() {
  //
  return;
}

/*
 |  =============
 |  MAIN FUNCTION
 |  =============
 */

int main(int argc, char **argv) {
  struct spb_instance spb;
  struct stat dir;
  struct SDL_Surface *s = NULL;
  struct pixel pix;
  struct atmos_bloop *bloop;
  int x, y, i;
  //animation stuff
  int current_frame;
  int frame_digits = (int)ceil(log10(FRAMES));
  char frame_fmt_str[MAX_STR];
  char frame_file[MAX_STR];
  
  //initialize stuff
  global_init();
  fprintf(stdout, "WINDOW_ANGLE: %lf\nIMAGE_WIDTH: %d\nIMAGE_HEIGHT: %d\n",WINDOW_ANGLE,IMAGE_WIDTH,IMAGE_HEIGHT);
  srand(235432);
  snprintf(frame_fmt_str, MAX_STR, "%s/%%0%dd.png", FRAME_FOLDER, frame_digits);
  if (
    atmos_init() == -1 ||
    bloop_init() == -1 ||
    contour_init() == -1 ||
    ray_init() == -1
  ) {
    return 1;
  }
  //make sure output folder exists
  if (stat(FRAME_FOLDER, &dir) == 0) {
    if (!S_ISDIR(dir.st_mode)) {
      fprintf(stderr, "Path '%s' exists but is not a folder\n",FRAME_FOLDER);
      return 1;
    }
  } else {
    if (errno == ENOENT) {
      if (mkdir(FRAME_FOLDER, 0700) != 0) {
        fprintf(stderr, "mkdir() on '%s': %s\n", FRAME_FOLDER, strerror(errno));
        return 1;
      }
    } else {
      fprintf(stderr, "stat() on '%s': %s\n", FRAME_FOLDER, strerror(errno));
      return 1;
    }
  }
  
  spb.real_goal = BLOOP_NUM*FRAMES;
  spb.bar_goal = 20;
  spb_init(&spb,"",NULL);
  for (current_frame=1; current_frame <= FRAMES; current_frame++) {
    
    //start with atmosphere baseline
    for (y=0; y < IMAGE_HEIGHT; y++) {
      for (x=0; x < IMAGE_WIDTH; x++) {
        atmos[y][x] = atmos_baseline(x,y);
      }
    }
    
    //apply bloops
    for (i=0; i < BLOOP_NUM; i++) {
      bloop = &(bloop_list[i]);
      bloop_apply(current_frame,bloop);
      spb.real_progress++;
      spb_update(&spb);
    }
    
    //render image
    if ((s = SDL_CreateRGBSurface(0,IMAGE_WIDTH,IMAGE_HEIGHT,24,0,0,0,0)) == NULL) {
      fprintf(stderr, "Failed to create SDL_Surface.\n");
      return -1;
    }
    for (y=0; y < IMAGE_HEIGHT; y++) {
      for (x=0; x < IMAGE_WIDTH; x++) {
        //are we inside the wedge-shaped window?
        if (atmos_bounds(x,y)) {
          
          /*
           |  LAYER 1
           |  density colors
           */
          density_to_color(&pix,atmos[y][x],x,y);
          
          /*
           |  LAYER 2
           |  contour lines
           */
          if (contour_detect(x,y)) {
            pix.r += 0.3;
            pix.g += 0.3;
            pix.b += 0.3;
          }
          
        } else {
          //outside window, everything is black
          pix.r = pix.g = pix.b = 0.0;
        }
        //all done, let's render this pixel
        pixel_insert(s,pix,x,y);
      }
    }
    //output image file
    snprintf(frame_file, MAX_STR, frame_fmt_str, current_frame);
    IMG_SavePNG(s,frame_file);
    SDL_FreeSurface(s);
    
  }
  
  //clean up
  atmos_free();
  free(bloop_list);
  free(contour_list);
  free(sight.nodes);
  return 0;
}

