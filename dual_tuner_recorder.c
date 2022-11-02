/* simple C program to record to file the I/Q streams
 * from a RSPduo in dual tuner mode
 * Franco Venturi - Sat Oct 29 02:36:31 PM EDT 2022
 */

/*
 * Copyright 2022 Franco Venturi.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <sdrplay_api.h>

#define UNUSED(x) (void)(x)
#define MAX_PATH_SIZE 1024

typedef struct {
    struct timeval earliest_callback;
    struct timeval latest_callback;
    unsigned long long total_samples;
    unsigned int next_sample_num;
    int output_fd;
    char rx_id;
} RXContext;

static void usage(const char* progname);
static void rxA_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
static void rxB_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
static void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext);
static void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, RXContext *rxContext);


int main(int argc, char *argv[])
{
    const char *serial_number = NULL;
    double rspduo_sample_rate = 0.0;
    int decimation = 1;
    sdrplay_api_If_kHzT if_frequency = sdrplay_api_IF_Zero;
    sdrplay_api_Bw_MHzT if_bandwidth = sdrplay_api_BW_0_200;
    sdrplay_api_AgcControlT agc = sdrplay_api_AGC_DISABLE;
    int gRdB = 40;
    int LNAstate = 0;
    double frequency = 100e6;
    int streaming_time = 10;  /* streaming time in seconds */
    const char *output_file = NULL;

    int c;
    while ((c = getopt(argc, argv, "s:r:d:i:b:g:l:f:x:o:h")) != -1) {
        switch (c) {
            case 's':
                serial_number = optarg;
                break;
            case 'r':
                if (sscanf(optarg, "%lg", &rspduo_sample_rate) != 1) {
                    fprintf(stderr, "invalid RSPduo sample rate: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'd':
                if (sscanf(optarg, "%d", &decimation) != 1) {
                    fprintf(stderr, "invalid decimation: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'i':
                if (sscanf(optarg, "%d", (int *)(&if_frequency)) != 1) {
                    fprintf(stderr, "invalid IF frequency: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'b':
                if (sscanf(optarg, "%d", (int *)(&if_bandwidth)) != 1) {
                    fprintf(stderr, "invalid IF bandwidth: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'g':
                if (strcmp(optarg, "AGC")) {
                    agc = sdrplay_api_AGC_50HZ;
                } else if (sscanf(optarg, "%d", &gRdB) != 1) {
                    fprintf(stderr, "invalid IF gain reduction: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'l':
                if (sscanf(optarg, "%d", &LNAstate) != 1) {
                    fprintf(stderr, "invalid LNA state: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'f':
                if (sscanf(optarg, "%lg", &frequency) != 1) {
                    fprintf(stderr, "invalid frequency: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'x':
                if (sscanf(optarg, "%d", &streaming_time) != 1) {
                    fprintf(stderr, "invalid streaming time: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'o':
                output_file = optarg;
                break;

            // help
            case 'h':
                usage(argv[0]);
                exit(0);
            case '?':
            default:
                usage(argv[0]);
                exit(1);
        }
    }

    /* open SDRplay API and check version */
    sdrplay_api_ErrT err;
    err = sdrplay_api_Open();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Open() failed: %s\n", sdrplay_api_GetErrorString(err));
        exit(1);
    }
    float ver;
    err = sdrplay_api_ApiVersion(&ver);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_ApiVersion() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }       
    if (ver != SDRPLAY_API_VERSION) {
        fprintf(stderr, "SDRplay API version mismatch - expected=%.2f found=%.2f\n", SDRPLAY_API_VERSION, ver);
        sdrplay_api_Close();
        exit(1);
    }

    /* select device */
    err = sdrplay_api_LockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_LockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }
#ifdef SDRPLAY_MAX_DEVICES
#undef SDRPLAY_MAX_DEVICES
#endif
#define SDRPLAY_MAX_DEVICES 4
    unsigned int ndevices = SDRPLAY_MAX_DEVICES;
    sdrplay_api_DeviceT devices[SDRPLAY_MAX_DEVICES];
    err = sdrplay_api_GetDevices(devices, &ndevices, ndevices);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_GetDevices() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }
    int device_index = -1;
    for (unsigned int i = 0; i < ndevices; i++) {
        /* we are only interested in RSPduo's */
        if (devices[i].hwVer == SDRPLAY_RSPduo_ID) {
            if (serial_number == NULL || strcmp(devices[i].SerNo, serial_number) == 0) {
                device_index = i;
                break;
            }
        }
    }
    if (device_index == -1) {
        fprintf(stderr, "SDRplay RSPduo not found or not available\n");
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }
    sdrplay_api_DeviceT device = devices[device_index];

    /* select RSPduo dual tuner mode */
    if ((device.rspDuoMode & sdrplay_api_RspDuoMode_Dual_Tuner) != sdrplay_api_RspDuoMode_Dual_Tuner ||
        (device.tuner & sdrplay_api_Tuner_Both) != sdrplay_api_Tuner_Both) {
        fprintf(stderr, "SDRplay RSPduo dual tuner mode not available\n");
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }
    device.tuner = sdrplay_api_Tuner_Both;
    device.rspDuoMode = sdrplay_api_RspDuoMode_Dual_Tuner;
    device.rspDuoSampleFreq = rspduo_sample_rate;

    err = sdrplay_api_SelectDevice(&device);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_SelectDevice() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }

    err = sdrplay_api_UnlockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_UnlockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    // select device settings
    sdrplay_api_DeviceParamsT *device_params;
    err = sdrplay_api_GetDeviceParams(device.dev, &device_params);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_GetDeviceParams() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }
    sdrplay_api_RxChannelParamsT *rx_channelA_params = device_params->rxChannelA ;
    sdrplay_api_RxChannelParamsT *rx_channelB_params = device_params->rxChannelB ;
    device_params->devParams->fsFreq.fsHz = rspduo_sample_rate;
    rx_channelA_params->ctrlParams.decimation.enable = decimation > 1;
    rx_channelB_params->ctrlParams.decimation.enable = decimation > 1;
    rx_channelA_params->ctrlParams.decimation.decimationFactor = decimation;
    rx_channelB_params->ctrlParams.decimation.decimationFactor = decimation;
    rx_channelA_params->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
    rx_channelA_params->tunerParams.ifType = if_frequency;
    rx_channelB_params->tunerParams.ifType = if_frequency;
    rx_channelA_params->tunerParams.bwType = if_bandwidth;
    rx_channelB_params->tunerParams.bwType = if_bandwidth;
    rx_channelA_params->ctrlParams.agc.enable = agc;
    rx_channelB_params->ctrlParams.agc.enable = agc;
    if (agc == sdrplay_api_AGC_DISABLE) {
        rx_channelA_params->tunerParams.gain.gRdB = gRdB;
        rx_channelB_params->tunerParams.gain.gRdB = gRdB;
    }
    rx_channelA_params->tunerParams.gain.LNAstate = LNAstate;
    rx_channelB_params->tunerParams.gain.LNAstate = LNAstate;
    rx_channelA_params->tunerParams.rfFreq.rfHz = frequency;
    rx_channelB_params->tunerParams.rfFreq.rfHz = frequency;

    /* quick check */
    sdrplay_api_CallbackFnsT callbackNullFns = { NULL, NULL, NULL };
    err = sdrplay_api_Init(device.dev, &callbackNullFns, NULL);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    /* print settings */
    fprintf(stdout, "SerNo=%s hwVer=%d tuner=0x%02x rspDuoMode=0x%02x rspDuoSampleFreq=%.0lf\n", device.SerNo, device.hwVer, device.tuner, device.rspDuoMode, device.rspDuoSampleFreq);
    fprintf(stdout, "RX A - LO=%.0lf BW=%d If=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channelA_params->tunerParams.rfFreq.rfHz, rx_channelA_params->tunerParams.bwType, rx_channelA_params->tunerParams.ifType, rx_channelA_params->ctrlParams.decimation.decimationFactor, rx_channelA_params->ctrlParams.agc.enable, rx_channelA_params->tunerParams.gain.gRdB, rx_channelA_params->tunerParams.gain.LNAstate);
    fprintf(stdout, "RX B - LO=%.0lf BW=%d If=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channelB_params->tunerParams.rfFreq.rfHz, rx_channelB_params->tunerParams.bwType, rx_channelB_params->tunerParams.ifType, rx_channelB_params->ctrlParams.decimation.decimationFactor, rx_channelB_params->ctrlParams.agc.enable, rx_channelB_params->tunerParams.gain.gRdB, rx_channelB_params->tunerParams.gain.LNAstate);

    int init_ok = 1;
    if (device.tuner != sdrplay_api_Tuner_Both) {
        fprintf(stderr, "unexpected change - tuner: 0x%02x -> 0x%02x\n", sdrplay_api_Tuner_Both, device.tuner);
        init_ok = 0;
    }
    if (device.rspDuoMode != sdrplay_api_RspDuoMode_Dual_Tuner) {
        fprintf(stderr, "unexpected change - rspDuoMode: 0x%02x -> 0x%02x\n", sdrplay_api_RspDuoMode_Dual_Tuner, device.rspDuoMode);
        init_ok = 0;
    }
    if (device.rspDuoSampleFreq != rspduo_sample_rate) {
        fprintf(stderr, "unexpected change - rspDuoSampleFreq: %.0lf -> %.0lf\n", rspduo_sample_rate, device.rspDuoSampleFreq);
        init_ok = 0;
    }
    if (device_params->devParams->fsFreq.fsHz != rspduo_sample_rate) {
        fprintf(stderr, "unexpected change - fsHz: %.0lf -> %.0lf\n", rspduo_sample_rate, device_params->devParams->fsFreq.fsHz);
        init_ok = 0;
    }
    if (rx_channelA_params->ctrlParams.decimation.enable != (decimation > 1)) {
        fprintf(stderr, "unexpected change - RX A decimation.enable: %d -> %d\n", decimation > 1, rx_channelA_params->ctrlParams.decimation.enable);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.decimation.enable != (decimation > 1)) {
        fprintf(stderr, "unexpected change - RX B decimation.enable: %d -> %d\n", decimation > 1, rx_channelB_params->ctrlParams.decimation.enable);
        init_ok = 0;
    }
    if (rx_channelA_params->ctrlParams.decimation.decimationFactor != decimation) {
        fprintf(stderr, "unexpected change - RX A decimation.decimationFactor: %d -> %d\n", decimation, rx_channelA_params->ctrlParams.decimation.decimationFactor);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.decimation.decimationFactor != decimation) {
        fprintf(stderr, "unexpected change - RX B decimation.decimationFactor: %d -> %d\n", decimation, rx_channelB_params->ctrlParams.decimation.decimationFactor);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.ifType != if_frequency) {
        fprintf(stderr, "unexpected change - RX A ifType: %d -> %d\n", if_frequency, rx_channelA_params->tunerParams.ifType);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.ifType != if_frequency) {
        fprintf(stderr, "unexpected change - RX B ifType: %d -> %d\n", if_frequency, rx_channelB_params->tunerParams.ifType);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.bwType != if_bandwidth) {
        fprintf(stderr, "unexpected change - RX A bwType: %d -> %d\n", if_bandwidth, rx_channelA_params->tunerParams.bwType);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.bwType != if_bandwidth) {
        fprintf(stderr, "unexpected change - RX B bwType: %d -> %d\n", if_bandwidth, rx_channelB_params->tunerParams.bwType);
        init_ok = 0;
    }
    if (rx_channelA_params->ctrlParams.agc.enable != agc) {
        fprintf(stderr, "unexpected change - RX A agc.enable: %d -> %d\n", agc, rx_channelA_params->ctrlParams.agc.enable);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.agc.enable != agc) {
        fprintf(stderr, "unexpected change - RX B agc.enable: %d -> %d\n", agc, rx_channelB_params->ctrlParams.agc.enable);
        init_ok = 0;
    }
    if (agc == sdrplay_api_AGC_DISABLE) {
        if (rx_channelA_params->tunerParams.gain.gRdB != gRdB) {
            fprintf(stderr, "unexpected change - RX A gain.gRdB: %d -> %d\n", gRdB, rx_channelA_params->tunerParams.gain.gRdB);
            init_ok = 0;
        }
        if (rx_channelB_params->tunerParams.gain.gRdB != gRdB) {
            fprintf(stderr, "unexpected change - RX B gain.gRdB: %d -> %d\n", gRdB, rx_channelB_params->tunerParams.gain.gRdB);
            init_ok = 0;
        }
    }
    if (rx_channelA_params->tunerParams.gain.LNAstate != LNAstate) {
        fprintf(stderr, "unexpected change - RX A gain.LNAstate: %d -> %d\n", LNAstate, rx_channelA_params->tunerParams.gain.LNAstate);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.gain.LNAstate != LNAstate) {
        fprintf(stderr, "unexpected change - RX B gain.LNAstate: %d -> %d\n", LNAstate, rx_channelB_params->tunerParams.gain.LNAstate);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.rfFreq.rfHz != frequency) {
        fprintf(stderr, "unexpected change - RX A rfHz: %.0lf -> %.0lf\n", frequency, rx_channelA_params->tunerParams.rfFreq.rfHz);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.rfFreq.rfHz != frequency) {
        fprintf(stderr, "unexpected change - RX B rfHz: %.0lf -> %.0lf\n", frequency, rx_channelB_params->tunerParams.rfFreq.rfHz);
        init_ok = 0;
    }

    if (!init_ok) {
        sdrplay_api_Uninit(device.dev);
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    err = sdrplay_api_Uninit(device.dev);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Uninit() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    /* now for the real thing */
    RXContext rx_contexts[] = {
        { .earliest_callback = {0, 0},
          .latest_callback = {0, 0},
          .total_samples = 0,
          .next_sample_num = 0xffffffff,
          .output_fd = -1,
          .rx_id = 'A'
        },
        { .earliest_callback = {0, 0},
          .latest_callback = {0, 0},
          .total_samples = 0,
          .next_sample_num = 0xffffffff,
          .output_fd = -1,
          .rx_id = 'B'
        }
    };

    sdrplay_api_CallbackFnsT callbackFns = {
        rxA_callback,
        rxB_callback,
        event_callback
    };

    if (output_file != NULL) {
        for (int i = 0; i < 2; i++) {
            char filename[MAX_PATH_SIZE];
            snprintf(filename, MAX_PATH_SIZE, output_file, rx_contexts[i].rx_id);
            int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd == -1) {
                fprintf(stderr, "open(%s) for writing failed: %s\n", filename, strerror(errno));
                for (int j = 0; j < i; j++) {
                    if (rx_contexts[j].output_fd > 0) {
                        close(rx_contexts[j].output_fd);
                    }
                }
                sdrplay_api_ReleaseDevice(&device);
                sdrplay_api_Close();
                exit(1);
            }
            rx_contexts[i].output_fd = fd;
        }
    }

    err = sdrplay_api_Init(device.dev, &callbackFns, rx_contexts);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    fprintf(stderr, "streaming for %d seconds\n", streaming_time);
    sleep(streaming_time);

    err = sdrplay_api_Uninit(device.dev);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Uninit() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    /* wait one second after sdrplay_api_Uninit() before closing the files */
    sleep(1);

    for (int i = 0; i < 2; i++) {
        if (rx_contexts[i].output_fd > 0) {
            if (close(rx_contexts[i].output_fd) == -1) {
                fprintf(stderr, "close(%d) failed: %s\n", rx_contexts[i].output_fd, strerror(errno));
            }
        }
    }

    for (int i = 0; i < 2; i++) {
        RXContext *rx_context = &rx_contexts[i];
        /* estimate actual sample rate */
        double elapsed_sec = (rx_context->latest_callback.tv_sec - rx_context->earliest_callback.tv_sec) + 1e-6 * (rx_context->latest_callback.tv_usec - rx_context->earliest_callback.tv_usec);
        double actual_sample_rate = (double)(rx_context->total_samples) / elapsed_sec;
        int rounded_sample_rate_kHz = (int)(actual_sample_rate / 1000.0 + 0.5);
        fprintf(stderr, "RX %c - total_samples=%llu actual_sample_rate=%.0lf rounded_sample_rate_kHz=%d\n", rx_context->rx_id, rx_context->total_samples, actual_sample_rate, rounded_sample_rate_kHz);
        const char *samplerate_string = "SAMPLERATE";
        if (output_file != NULL && strstr(output_file, samplerate_string)) {
            char old_filename[MAX_PATH_SIZE];
            snprintf(old_filename, MAX_PATH_SIZE, output_file, rx_context->rx_id);
            char *p = strstr(old_filename, samplerate_string);
            int from = p - old_filename;
            int to = from + strlen(samplerate_string);
            char new_filename[MAX_PATH_SIZE];
            snprintf(new_filename, MAX_PATH_SIZE, "%.*s%d%s", from, old_filename, rounded_sample_rate_kHz, old_filename + to);
            if (rename(old_filename, new_filename) == -1) {
                fprintf(stderr, "rename(%s, %s) failed: %s\n", old_filename, new_filename, strerror(errno));
            }
        }
    }


    err = sdrplay_api_LockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_LockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }
    err = sdrplay_api_ReleaseDevice(&device);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_ReleaseDevice() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        sdrplay_api_Close();
        exit(1);
    }
    err = sdrplay_api_UnlockDeviceApi();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_UnlockDeviceApi() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_Close();
        exit(1);
    }

    /* all done: close SDRplay API */
    err = sdrplay_api_Close();
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Close() failed: %s\n", sdrplay_api_GetErrorString(err));
        exit(1);
    }

    return 0;
}

static void usage(const char* progname)
{
    fprintf(stderr, "usage: %s [options...]\n", progname);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "    -s <serial number>\n");
    fprintf(stderr, "    -r <RSPduo sample rate>\n");
    fprintf(stderr, "    -d <decimation>\n");
    fprintf(stderr, "    -i <IF frequency>\n");
    fprintf(stderr, "    -b <IF bandwidth>\n");
    fprintf(stderr, "    -g <IF gain reduction> (\"AGC\" to enable AGC)\n");
    fprintf(stderr, "    -l <LNA state>\n");
    fprintf(stderr, "    -f <center frequency>\n");
    fprintf(stderr, "    -s <streaming time (s)> (default: 10s)\n");
    fprintf(stderr, "    -h show usage\n");
}

static void rxA_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    rx_callback(xi, xq, params, numSamples, reset, &(((RXContext *)cbContext)[0]));
}

static void rxB_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext)
{
    rx_callback(xi, xq, params, numSamples, reset, &(((RXContext *)cbContext)[1]));
}

static void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext)
{
    UNUSED(eventId);
    UNUSED(tuner);
    UNUSED(params);
    UNUSED(cbContext);
    /* do nothing for now */
    return;
}

static void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, RXContext *rxContext)
{
    UNUSED(reset);

    /* track callback timestamp */
    gettimeofday(&rxContext->latest_callback, NULL);
    if (rxContext->earliest_callback.tv_sec == 0) {
        rxContext->earliest_callback.tv_sec = rxContext->latest_callback.tv_sec;
        rxContext->earliest_callback.tv_usec = rxContext->latest_callback.tv_usec;
    }
    rxContext->total_samples += numSamples;

    /* check for dropped samples */
    if (rxContext->next_sample_num != 0xffffffff && params->firstSampleNum != rxContext->next_sample_num) {
        unsigned int dropped_samples;
        if (rxContext->next_sample_num < params->firstSampleNum) {
            dropped_samples = params->firstSampleNum - rxContext->next_sample_num;
        } else {
            dropped_samples = UINT_MAX - (params->firstSampleNum - rxContext->next_sample_num) + 1;
        }
        fprintf(stderr, "RX %c - dropped %d samples\n", rxContext->rx_id, dropped_samples);
    }
    rxContext->next_sample_num = params->firstSampleNum + numSamples;

    /* write samples to output file */
    if (rxContext->output_fd > 0) {
        short samples[4096];
        for (unsigned int i = 0; i < numSamples; i++) {
            samples[2*i] = xi[i];
        }
        for (unsigned int i = 0; i < numSamples; i++) {
            samples[2*i+1] = xq[i];
        }
        size_t count = numSamples * 2 * sizeof(short);
        ssize_t nwritten = write(rxContext->output_fd, samples, count);
        if (nwritten == -1) {
            fprintf(stderr, "RX %c - write() failed: %s\n", rxContext->rx_id, strerror(errno));
        } else if ((size_t)nwritten != count) {
            fprintf(stderr, "RX %c - incomplete write() - expected: %ld bytes - actual: %ld bytes\n", rxContext->rx_id, count, nwritten);
        }
    }
}