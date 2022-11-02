# RSPduo dual tuner mode experiments

A collection of tools, programs, and scripts to experiment with the SDRplay RSPduo in dual tuner mode

## dual_tuner_recorder

A simple C program that records to two files the I/Q streams for the A and B channels from an RSPduo in dual tuner mode; sample rate, decimation, IF frequency, IF bandwidth, gains, and center frequency are provided via command line arguments (see below).

To build it just run `make` using the provided Makefile.

These are the command line options for `dual_tuner_recorder`:

    - -s <serial number>
    - -r <RSPduo sample rate>
    - -d <decimation>
    - -i <IF frequency>
    - -b <IF bandwidth>
    - -g <IF gain reduction> ("AGC" to enable AGC)
    - -l <LNA state>
    - -f <center frequency>
    - -s <streaming time (s)> (default: 10s)


Here are some usage examples:

- record local NOAA weather radio on 162.55MHz using an RSPduo sample rate of 6MHz and IF=1620kHz:
```
./dual_tuner_recorder -r 6000000 -i 1620 -b 1536 -l 3 -f 162550000 -o noaa-6M-SAMPLERATEk-%c.raw
```

- record local NOAA weather radio on 162.55MHz using an RSPduo sample rate of 8MHz and IF=2048kHz:
```
./dual_tuner_recorder -r 8000000 -i 2048 -b 1536 -l 3 -f 162550000 -o noaa-8M-SAMPLERATEk-%c.raw
```

## fm_player

A simple Python script that demodulates a file containing an I/Q stream contaning a NBFM signal (see `dual_tuner_recorder` above) and shows a frequency plot of the I/Q stream.
This script uses GNU Radio blocks and therefore requires a recent version of GNU Radio to run.

These are the command line options for `fm_player`:

    - -i <input file> - mandatory
    - -s <sample rate> - mandatory
    - -o <frequency offset> - default: 0
    - -N (FM block: NBFM receive)
    - -D (FM block: FM demod)
    - -v <volume> - default: 0.3
    - -f <center frequency for frequency display> - default: 0
    - -W (wait for user input) - default: False


Here are some usage examples:

- play the I/Q stream for channel A from the first example above (please note that the estimated sample rate in the file name might be slightly different; for instance in my case it was 'noaa-6M-2002k-A.raw'; you may still want to use the command line option `-s 2e6`):
```
./fm_player.py -i noaa-6M-2000k-A.raw -s 2e6 -f 162550000
```

- play the I/Q stream for channel B from the second example above (see note in the previous example about the I/Q stream recording file name):
```
./fm_player.py -i noaa-8M-2000k-B.raw -s 2e6 -f 162550000
```


## Copyright

(C) 2022 Franco Venturi - Licensed under the GNU GPL V3 (see [LICENSE](LICENSE))
