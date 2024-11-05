#!/bin/sh

for i in frames/* ; do
	j=`echo $i | cut -d'/' -f2`
	convert art/atmos_sim-chart-base-wide.png \( $i -resize 4521x1018\! \) -geometry +150+60 -compose LinearDodge -composite frames-edit/$j
done

