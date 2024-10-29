#include <stdio.h>
#include <math.h>
#include <string.h>
#include "SDL2/SDL_image.h"
#include "SDL2/SDL.h"
//#include "anl.h"
#include "spb.h"
#include "vector3D.h"

#define MAX_STR 1024

#define EARTH_RADIUS 6371.0 // kilometers
#define EARTH_CIRCUMFERENCE 40030.173592041 // kilometers
#define WINDOW_ARC_LENGTH 400.0 // kilometers
#define WINDOW_ALTITUDE 35.0 // kilometers

#define IMAGE_RES 10.0 // pixels per kilometer

#define IMAGE_OUTPUT "out.png"

//let's caculate some global variables based on the input metrics above
double WINDOW_ANGLE, WINDOW_TOP, WINDOW_RIGHT, WINDOW_LEFT, WINDOW_BOTTOM;
int IMAGE_WIDTH, IMAGE_HEIGHT;
void global_init() {
  WINDOW_ANGLE = (WINDOW_ARC_LENGTH/EARTH_CIRCUMFERENCE) * 360.0; // degrees
  WINDOW_TOP = EARTH_RADIUS + WINDOW_ALTITUDE; // kilometers
  WINDOW_RIGHT = sin((WINDOW_ANGLE/2.0) * (PI/180.0)) * WINDOW_TOP; // kilometers
  WINDOW_LEFT = -WINDOW_RIGHT; // kilometers
  WINDOW_BOTTOM = cos((WINDOW_ANGLE/2.0) * (PI/180.0)) * EARTH_RADIUS; // kilometers
  IMAGE_WIDTH = (int)ceil( (WINDOW_RIGHT-WINDOW_LEFT) * IMAGE_RES ); // pixels
  IMAGE_HEIGHT = (int)ceil( (WINDOW_TOP-WINDOW_BOTTOM) * IMAGE_RES ); // pixels
}

struct pixel {
  double r, g, b;
};
struct atmos_coord {
  double alt; // altitude in kilometers
  double ground; // ground point in kilometers
};
struct atmos_grade_stop {
  double alt; // altitude in kilometers
  double density; // density in kg/m^3
};

//atmspheric density field, in kg/m^3
double **atmos;
//atmospheric baseline density gradient
#define ATMOS_STOP_NUM 100
struct atmos_grade_stop atmos_grade[ATMOS_STOP_NUM];

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

//initialize stuff
int atmos_init() {
  int x, y, i, halfway;
  double n1x, n1y, h1x, h1y, h2x, h2y, n2x, n2y;
  double frac;
  
  //atmospheric density gradient
  //https://commons.wikimedia.org/wiki/File:Comparison_US_standard_atmosphere_1962.svg
  /*
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
    fprintf(stderr, "calloc() returned NULL\n");
    return -1;
  }
  for (y=0; y < IMAGE_HEIGHT; y++) {
    if ((atmos[y] = (double *)calloc(sizeof(double), IMAGE_WIDTH)) == NULL) {
      fprintf(stderr, "calloc() returned NULL\n");
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

struct grade_stop {
  double val;
  struct pixel color;
};
void density_to_color(struct pixel *pix, double density, int x, int y) {
  int stop_num = 5;
  struct grade_stop grade[stop_num], *floor, *ceil;
  int i;
  double frac, max;
  max = 1.3;
  grade[0].val = 0.0;
  grade[0].color.r = 0.05; grade[0].color.g = 0.05; grade[0].color.b = 0.05;
  grade[1].val = (1.0/(stop_num-1))*max;
  grade[1].color.r = 0.00; grade[1].color.g = 0.00; grade[1].color.b = 0.20;
  grade[2].val = (2.0/(stop_num-1))*max;
  grade[2].color.r = 0.00; grade[2].color.g = 0.18; grade[2].color.b = 0.20;
  grade[3].val = (3.0/(stop_num-1))*max;
  grade[3].color.r = 0.20; grade[3].color.g = 0.20; grade[3].color.b = 0.00;
  grade[4].val = (4.0/(stop_num-1))*max;
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

void pixel_insert(struct SDL_Surface *s, struct pixel p, int x, int y) {
  ((Uint8 *)s->pixels)[y*s->pitch+x*3+0] = (Uint8)(255.0*fmax(0,fmin(1,p.b)));
  ((Uint8 *)s->pixels)[y*s->pitch+x*3+1] = (Uint8)(255.0*fmax(0,fmin(1,p.g)));
  ((Uint8 *)s->pixels)[y*s->pitch+x*3+2] = (Uint8)(255.0*fmax(0,fmin(1,p.r)));
}

int main(int argc, char **argv) {
  struct SDL_Surface *s = NULL;
  struct pixel pix;
  int x, y;
  global_init();
  fprintf(stdout, "WINDOW_ANGLE: %lf\nIMAGE_WIDTH: %d\nIMAGE_HEIGHT: %d\n",WINDOW_ANGLE,IMAGE_WIDTH,IMAGE_HEIGHT);
  if (atmos_init() == -1) {
    return 1;
  }
  
  if ((s = SDL_CreateRGBSurface(0,IMAGE_WIDTH,IMAGE_HEIGHT,24,0,0,0,0)) == NULL) {
    fprintf(stderr,"Failed to create SDL_Surface.\n");
    return -1;
  }
  for (y=0; y < IMAGE_HEIGHT; y++) {
    for (x=0; x < IMAGE_WIDTH; x++) {
      if (atmos_bounds(x,y)) {
        density_to_color(&pix,atmos[y][x],x,y);
      } else {
        pix.r = pix.g = pix.b = 0.0;
      }
      pixel_insert(s,pix,x,y);
    }
  }
  IMG_SavePNG(s,IMAGE_OUTPUT);
  SDL_FreeSurface(s);
  
  atmos_free();
  return 0;
}

