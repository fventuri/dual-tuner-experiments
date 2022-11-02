#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# simple Python script that plays a file with an I/Q stream 
# demodulating a NBFM signal using GNU Radio blocks
# Franco Venturi - Tue Nov  1 09:58:44 PM EDT 2022

# Copyright 2022 Franco Venturi.
# SPDX-License-Identifier: GPL-3.0-or-later


import getopt
import signal
import sys
import threading

from PyQt5 import Qt
import sip

from gnuradio import analog
from gnuradio import audio
from gnuradio import blocks
from gnuradio import filter
from gnuradio import gr
from gnuradio import qtgui
from gnuradio.fft import window
from gnuradio.filter import firdes


def usage():
    print('usage:', sys.argv[0], 'options...', file=sys.stderr)
    print('options:', file=sys.stderr)
    print('    -i <input file> - mandatory', file=sys.stderr)
    print('    -s <sample rate> - mandatory', file=sys.stderr)
    print('    -o <frequency offset> - default: 0', file=sys.stderr)
    print('    -N (FM block: NBFM receive)', file=sys.stderr)
    print('    -D (FM block: FM demod)', file=sys.stderr)
    print('    -v <volume> - default: 0.3', file=sys.stderr)
    print('    -f <center frequency for frequency display> - default: 0', file=sys.stderr)
    print('    -W (wait for user input) - default: False', file=sys.stderr)
    print('    -h (shows this help)', file=sys.stderr)

def run_gnuradio_flowgraph(tb, wait_for_user_input):
    tb.start()
    if wait_for_user_input:
        input('Press Enter to quit: ')
    else:
        tb.wait()
    tb.stop()
    Qt.QApplication.quit()

def main():
    input_file = None
    input_sample_rate = 0
    frequency_offset = 0
    nbfm_receive = True
    volume = 0.3
    center_frequency = 0
    wait_for_user_input = False

    try:
        opts, args = getopt.getopt(sys.argv[1:], 'i:s:o:NDv:f:Wh')
    except getopt.GetoptError:
        usage()
        sys.exit(1)
    for o, a in opts:
        if o == '-i':
            input_file = a
        elif o == '-s':
            input_sample_rate = float(a)
        elif o == '-o':
            frequency_offset = float(a)
        elif o == '-N':
            nbfm_receive = True
        elif o == '-D':
            nbfm_receive = False
        elif o == '-v':
            volume = float(a)
        elif o == '-f':
            center_frequency = float(a)
        elif o == '-W':
            wait_for_user_input = True
        elif o == '-h':
            usage()
            sys.exit(0)
        else:
            usage()
            sys.exit(1)

    ##########
    # settings
    ##########
    rf_decimation = 10
    audio_decimation = 4
    deviation = 5e3
    audio_sample_rate = 48000  # needs to be int
    frequency_bins = 1024
    frequency_average_over_seconds = 0.1

    quadrature_sample_rate = input_sample_rate / rf_decimation
    audio_resampling_ratio = quadrature_sample_rate / audio_decimation / audio_sample_rate

    ########################
    # blocks and connections
    ########################
    tb = gr.top_block()

    file_source = blocks.file_source(gr.sizeof_short, input_file, False, 0, 0)

    interleaved_short_to_complex = blocks.interleaved_short_to_complex(False, False, 32767)
    tb.connect((file_source, 0), (interleaved_short_to_complex, 0))

    throttle = blocks.throttle(gr.sizeof_gr_complex, input_sample_rate, True)
    tb.connect((interleaved_short_to_complex, 0), (throttle, 0))

    fir_filter_taps = firdes.low_pass(1.0, input_sample_rate, 15e3, 1.5e3, window.WIN_HAMMING, 6.76)
    if not frequency_offset:
        fir_filter = filter.fir_filter_ccc(rf_decimation, fir_filter_taps)
    else:
        fir_filter = filter.freq_xlating_fir_filter_ccc(rf_decimation, fir_filter_taps, frequency_offset, input_sample_rate)
    tb.connect((throttle, 0), (fir_filter, 0))

    if not nbfm_receive:
        fm_block = analog.fm_demod_cf(channel_rate=quadrature_sample_rate,
                                      audio_decim=audio_decimation,
                                      deviation=deviation,
                                      audio_pass=15000,
                                      audio_stop=16000,
                                      gain=1.0,
                                      tau=75e-6
                                     )
    else:
        fm_block = analog.nbfm_rx(audio_rate=quadrature_sample_rate / audio_decimation,
                                  quad_rate=quadrature_sample_rate,
                                  tau=75e-6,
                                  max_dev=deviation,
                                 ) 
    tb.connect((fir_filter, 0), (fm_block, 0))

    resampler = filter.mmse_resampler_ff(0, audio_resampling_ratio)
    tb.connect((fm_block, 0), (resampler, 0))

    volume_control = blocks.multiply_const_ff(volume)
    tb.connect((resampler, 0), (volume_control, 0))

    audio_sink = audio.sink(audio_sample_rate, '', True)
    tb.connect((volume_control, 0), (audio_sink, 0))

    ####################
    # GUI frequency sink
    ####################
    qtgui_freq_sink = qtgui.freq_sink_c(
        frequency_bins, #size
        window.WIN_BLACKMAN_hARRIS, #wintype
        center_frequency, #fc
        input_sample_rate, #bw
        "", #name
        1,
        None # parent
    )
    qtgui_freq_sink.enable_grid(True)
    qtgui_freq_sink.set_fft_average(frequency_average_over_seconds)
    qtgui_freq_sink.set_line_label(0, input_file)
    qtgui_freq_sink.set_update_time(frequency_average_over_seconds)
    qtgui_freq_sink.set_y_axis(-140, 10)
    qtgui_freq_sink.set_y_label('Relative Gain', 'dB')
    tb.connect((throttle, 0), (qtgui_freq_sink, 0))

    qtgui_freq_sink_win = sip.wrapinstance(qtgui_freq_sink.qwidget(), Qt.QWidget)

    ###############
    # run flowgraph
    ###############
    qapp = Qt.QApplication(sys.argv)
    qtgui_freq_sink_win.show()

    # run the GNU Radio flowgraph in a different thread
    gnuradio_flowgraph = threading.Thread(target=run_gnuradio_flowgraph, args=(tb, wait_for_user_input))
    gnuradio_flowgraph.start()
    qapp.exec_()
    gnuradio_flowgraph.join()


if __name__ == '__main__':
    main()
