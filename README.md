# RSPduo dual tuner mode experiments

A collection of tools, programs, and scripts to experiment with the SDRplay RSPduo in dual tuner mode

## dual_tuner_recorder

A simple C program that records to two files the I/Q streams for the A and B channels from an RSPduo in dual tuner mode; sample rate, decimation, IF frequency, IF bandwidth, gains, and center frequency are provided via command line arguments (see below).


To build it run these commands:
```
mkdir build
cd build
cmake ..
make (or ninja)
```


These are the command line options for `dual_tuner_recorder`:

    -s <serial number>
    -r <RSPduo sample rate>
    -d <decimation>
    -i <IF frequency>
    -b <IF bandwidth>
    -g <IF gain reduction> ("AGC" to enable AGC)
    -l <LNA state>
    -D disable post tuner DC offset compensation (default: enabled)
    -I disable post tuner I/Q balance compensation (default: enabled)
    -y tuner DC offset compensation parameters <dcCal,speedUp,trackTime,refeshRateTime> (default: 3,0,1,2048)
    -f <center frequency>
    -x <streaming time (s)> (default: 10s)
    -o <output file> ('%c' will be replaced by the channel id (A or B) and 'SAMPLERATE' will be replaced by the estimated sample rate in kHz)


Here are some usage examples:

- record local NOAA weather radio on 162.55MHz using an RSPduo sample rate of 6MHz and IF=1620kHz:
```
./dual_tuner_recorder -r 6000000 -i 1620 -b 1536 -l 3 -f 162550000 -o noaa-6M-SAMPLERATEk-%c.iq16
```

- record local NOAA weather radio on 162.55MHz using an RSPduo sample rate of 8MHz and IF=2048kHz:
```
./dual_tuner_recorder -r 8000000 -i 2048 -b 1536 -l 3 -f 162550000 -o noaa-8M-SAMPLERATEk-%c.iq16
```

## fm_player

A simple Python script that demodulates a file containing an I/Q stream contaning a NBFM signal (see `dual_tuner_recorder` above) and shows a frequency plot of the I/Q stream.
This script uses GNU Radio blocks and therefore requires a recent version of GNU Radio to run.

These are the command line options for `fm_player`:

    -i <input file> - mandatory
    -s <sample rate> - mandatory
    -o <frequency offset> - default: 0
    -N (FM block: NBFM receive)
    -D (FM block: FM demod)
    -v <volume> - default: 0.3
    -f <center frequency for frequency display> - default: 0
    -W (wait for user input) - default: False


Here are some usage examples:

- play the I/Q stream for channel A from the first example above (please note that the estimated sample rate in the file name might be slightly different; for instance in my case it was 'noaa-6M-2002k-A.iq16'; you may still want to use the command line option `-s 2e6`):
```
./fm_player.py -i noaa-6M-2000k-A.iq16 -s 2e6 -f 162550000
```

- play the I/Q stream for channel B from the second example above (see note in the previous example about the I/Q stream recording file name):
```
./fm_player.py -i noaa-8M-2000k-B.iq16 -s 2e6 -f 162550000
```


## Copyright

(C) 2022 Franco Venturi - Licensed under the GNU GPL V3 (see [LICENSE](LICENSE))
# working on it
