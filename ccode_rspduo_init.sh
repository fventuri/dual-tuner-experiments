#!bin/bash
#rspduo codar shell script


PID=$(pidof sdrplay_apiService)

renice -n -20 -p $PID

#setting the sample rate for the dual tuner downconversion mode
#this must be the actual wanted sample rate on data times four (Samps/s)
sr=2000000

#setting the center frequency, you can set each tuner to an independent
#frequency by passing a comma separated list (Hz)
cf=13475000

#setting the bandwidth (kHz), note that the downconversion mode only
#accepts specific combinations of bw, IF, and sr in the API
bw=200

#setting the LNA (gain) of the radio (number 0-6)
lna=3

#setting length of data collection (s)
Time=6

#setting intermediate frequency (Hz) for downconversion mode, only specific
#combinations of bw, IF, and sr are allowed in the API
IF=450

#setting decimation value
dec=1
#creating timestamps for data collection filename
unix1970=$(date +'%s')
yyyy_mm_dd=$(date +"%Y%m%d")
MM_SS=$(date +"%H%M")

#switching to script directory
cd /home/ubuntu/dual-tuner-experiments/build

#executing script for data collection
nice --20 ./dual_tuner_recorder -i $IF -r $sr -b $bw -l $lna -f $cf -x $Time -d $dec -o "$unix1970"_CARL_"$yyyy_mm_dd"_"$MM_SS"_"$((cf / 1000))"_SAMPLERATE_--LOOPS_RSPduo--_%c.byn


