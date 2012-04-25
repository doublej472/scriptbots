#!/bin/bash

#PBS -q janus-small
#PBS -N scriptbots_qsub
#PBS -l walltime=24:00:00
#PBS -j oe
#PBS -l nodes=1:ppn=12

. /curc/tools/utils/dkinit

use Moab
use Torque

cd /lustre/janus_scratch/daco5652/scriptbots

# 24 hours = 60*60*24 = 86400 - 400 = 85000
./build/scriptbots -v -s 85000




