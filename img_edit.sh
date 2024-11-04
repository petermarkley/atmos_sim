#!/bin/sh

for i in frames/* ; do
	j=`echo $i | cut -d'/' -f2`
	convert art/atmos_sim-chart-base.png \( $i -resize 2011x763\! \) -geometry +100+60 -compose LinearDodge -composite frames-edit/$j
done

