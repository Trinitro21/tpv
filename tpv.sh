#!/bin/sh
gcc -o tpv tpv.c -levdev -lX11 -lXcomposite -lm -lXi -lXfixes
