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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sched.h>

#include <sdrplay_api.h>
#include <pigpio.h>

#define UNUSED(x) (void)(x)
#define MAX_PATH_SIZE 1024

//static volatile int g_reset_counts;
static int pin=17;
static uint32_t pin_mask;
//struct timeval begin;

typedef struct {
    uint32_t latest_tick;
    uint32_t pulse_count;
} gpioData_t; 

static volatile gpioData_t gpio_data;

typedef struct {
    struct timeval earliest_callback;
    struct timeval latest_callback;
    uint32_t gpio_tick;
    unsigned long long total_samples;
    unsigned int next_sample_num;
    int output_fd;
    short imin, imax;
    short qmin, qmax;
    char rx_id;
} RXContext;

static void usage(const char* progname);
static void rxA_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
static void rxB_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, void *cbContext);
static void event_callback(sdrplay_api_EventT eventId, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *cbContext);
static void rx_callback(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int numSamples, unsigned int reset, RXContext *rxContext);
static void samples(const gpioSample_t *samples, int numSamples);

int main(int argc, char *argv[])
{
    const char *serial_number = NULL;
    double rspduo_sample_rate = 0.0;
    int decimation_A = 1;
    int decimation_B = 1;
    sdrplay_api_If_kHzT if_frequency_A = sdrplay_api_IF_Zero;
    sdrplay_api_If_kHzT if_frequency_B = sdrplay_api_IF_Zero;
    sdrplay_api_Bw_MHzT if_bandwidth_A = sdrplay_api_BW_0_200;
    sdrplay_api_Bw_MHzT if_bandwidth_B = sdrplay_api_BW_0_200;
    sdrplay_api_AgcControlT agc_A = sdrplay_api_AGC_DISABLE;
    sdrplay_api_AgcControlT agc_B = sdrplay_api_AGC_DISABLE;
    int gRdB_A = 40;
    int gRdB_B = 40;
    int LNAstate_A = 0;
    int LNAstate_B = 0;
    int DCenable_A = 1;
    int DCenable_B = 1;
    int IQenable_A = 1;
    int IQenable_B = 1;
    // the next four parameters related to DC offset compensation can only
    // be set identical for both receivers to make things (and code) simpler
    int dcCal = 3;
    int speedUp = 0;
    int trackTime = 1;
    int refreshRateTime = 2048;
    double frequency_A = 100e6;
    double frequency_B = 100e6;
    int streaming_time = 10;  /* streaming time in seconds */
    const char *output_file = NULL;
    int debug_enable = 0;

    int cfg = gpioCfgGetInternals();
    cfg |= PI_CFG_NOSIGHANDLER;  // (1<<10)
    gpioCfgSetInternals(cfg);
    gpioCfgClock(5, 1, 1);

    gpioInitialise();
    gpioSetMode(pin, PI_INPUT);
    gpioSetPullUpDown(pin, PI_PUD_DOWN);

    int c;
    while ((c = getopt(argc, argv, "s:r:d:i:b:g:l:DIy:f:x:o:Lh")) != -1) {
        int n;
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
                n = sscanf(optarg, "%d,%d", &decimation_A, &decimation_B);
                if (n < 1) {
                    fprintf(stderr, "invalid decimation: %s\n", optarg);
                    exit(1);
                }
                if (n == 1)
                    decimation_B = decimation_A;
                break;
            case 'i':
                n = sscanf(optarg, "%d,%d", (int *)(&if_frequency_A), (int *)(&if_frequency_B));
                if (n < 1) {
                    fprintf(stderr, "invalid IF frequency: %s\n", optarg);
                    exit(1);
                }
                if (n == 1)
                    if_frequency_B = if_frequency_A;
                break;
            case 'b':
                n = sscanf(optarg, "%d,%d", (int *)(&if_bandwidth_A), (int *)(&if_bandwidth_B));
                if (n < 1) {
                    fprintf(stderr, "invalid IF bandwidth: %s\n", optarg);
                    exit(1);
                }
                if (n == 1)
                    if_bandwidth_B = if_bandwidth_A;
                break;
            case 'g':
                if (strcmp(optarg, "AGC") == 0 || strcmp(optarg, "AGC,AGC") == 0) {
                    agc_A = sdrplay_api_AGC_50HZ;
                    agc_B = sdrplay_api_AGC_50HZ;
                } else if (sscanf(optarg, "AGC,%d", &gRdB_B) == 1) {
                    agc_A = sdrplay_api_AGC_50HZ;
                } else if (sscanf(optarg, "%d,AGC", &gRdB_A) == 1) {
                    agc_B = sdrplay_api_AGC_50HZ;
                } else {
                    n = sscanf(optarg, "%d,%d", &gRdB_A, &gRdB_B);
                    if (n < 1) {
                        fprintf(stderr, "invalid IF gain reduction: %s\n", optarg);
                        exit(1);
                    }
                    if (n == 1)
                        gRdB_B = gRdB_A;
                }
                break;
            case 'l':
                n = sscanf(optarg, "%d,%d", &LNAstate_A, &LNAstate_B);
                if (n < 1) {
                    fprintf(stderr, "invalid LNA state: %s\n", optarg);
                    exit(1);
                }
                if (n == 1)
                    LNAstate_B = LNAstate_A;
                break;
            case 'D':
                DCenable_A = 0;
                DCenable_B = 0;
                break;
            case 'I':
                IQenable_A = 0;
                IQenable_B = 0;
                break;
            case 'y':
                if (sscanf(optarg, "%d,%d,%d,%d", &dcCal, &speedUp, &trackTime, &refreshRateTime) != 4) {
                    fprintf(stderr, "invalid tuner DC offset compensation parameters: %s\n", optarg);
                    exit(1);
                }
                break;
            case 'f':
                n = sscanf(optarg, "%lg,%lg", &frequency_A, &frequency_B);
                if (n < 1) {
                    fprintf(stderr, "invalid frequency: %s\n", optarg);
                    exit(1);
                }
                if (n == 1)
                    frequency_B = frequency_A;
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
            case 'L':
                debug_enable = 1;
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

    if (debug_enable) {
        err = sdrplay_api_DebugEnable(device.dev, sdrplay_api_DbgLvl_Verbose);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_DebugEnable() failed: %s\n", sdrplay_api_GetErrorString(err));
            sdrplay_api_ReleaseDevice(&device);
            sdrplay_api_Close();
            exit(1);
        }
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
    rx_channelA_params->ctrlParams.decimation.enable = decimation_A > 1;
    rx_channelA_params->ctrlParams.decimation.decimationFactor = decimation_A;
    rx_channelA_params->rspDuoTunerParams.tuner1AmPortSel = sdrplay_api_RspDuo_AMPORT_2;
    rx_channelA_params->tunerParams.ifType = if_frequency_A;
    rx_channelA_params->tunerParams.bwType = if_bandwidth_A;
    rx_channelA_params->ctrlParams.agc.enable = agc_A;
    if (agc_A == sdrplay_api_AGC_DISABLE) {
        rx_channelA_params->tunerParams.gain.gRdB = gRdB_A;
    }
    rx_channelA_params->tunerParams.gain.LNAstate = LNAstate_A;
    rx_channelA_params->ctrlParams.dcOffset.DCenable = DCenable_A;
    rx_channelA_params->ctrlParams.dcOffset.IQenable = IQenable_A;
    rx_channelA_params->tunerParams.dcOffsetTuner.dcCal = dcCal;
    rx_channelA_params->tunerParams.dcOffsetTuner.speedUp = speedUp;
    rx_channelA_params->tunerParams.dcOffsetTuner.trackTime = trackTime;
    rx_channelA_params->tunerParams.dcOffsetTuner.refreshRateTime = refreshRateTime;
    rx_channelB_params->tunerParams.dcOffsetTuner.dcCal = dcCal;
    rx_channelB_params->tunerParams.dcOffsetTuner.speedUp = speedUp;
    rx_channelB_params->tunerParams.dcOffsetTuner.trackTime = trackTime;
    rx_channelB_params->tunerParams.dcOffsetTuner.refreshRateTime = refreshRateTime;
    rx_channelA_params->tunerParams.rfFreq.rfHz = frequency_A;

    /* quick check */
    sdrplay_api_CallbackFnsT callbackNullFns = { NULL, NULL, NULL };
    err = sdrplay_api_Init(device.dev, &callbackNullFns, NULL);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }
    // since sdrplay_api_Init() resets channelB settings to channelA values,
    // we need to update all the settings for channelB that are different
    sdrplay_api_ReasonForUpdateT reason_for_update = sdrplay_api_Update_None;
    if (decimation_B != decimation_A) {
        rx_channelB_params->ctrlParams.decimation.enable = decimation_B > 1;
        rx_channelB_params->ctrlParams.decimation.decimationFactor = decimation_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_Decimation;
    }
    if (if_frequency_B != if_frequency_A) {
        rx_channelB_params->tunerParams.ifType = if_frequency_B;
        reason_for_update |= sdrplay_api_Update_Tuner_IfType;
    }
    if (if_bandwidth_B != if_bandwidth_A) {
        rx_channelB_params->tunerParams.bwType = if_bandwidth_B;
        reason_for_update |= sdrplay_api_Update_Tuner_BwType;
    }
    if (agc_B != agc_A) {
        rx_channelB_params->ctrlParams.agc.enable = agc_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_Agc;
    }
    if (agc_B == sdrplay_api_AGC_DISABLE) {
        if (gRdB_B != gRdB_A) {
            rx_channelB_params->tunerParams.gain.gRdB = gRdB_B;
            reason_for_update |= sdrplay_api_Update_Tuner_Gr;
        }
    }
    if (LNAstate_B != LNAstate_A) {
        rx_channelB_params->tunerParams.gain.LNAstate = LNAstate_B;
        reason_for_update |= sdrplay_api_Update_Tuner_Gr;
    }
    if (DCenable_B != DCenable_A) {
        rx_channelB_params->ctrlParams.dcOffset.DCenable = DCenable_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_DCoffsetIQimbalance;
    }
    if (IQenable_B != IQenable_A) {
        rx_channelB_params->ctrlParams.dcOffset.IQenable = IQenable_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_DCoffsetIQimbalance;
    }
    if (frequency_B != frequency_A) {
        rx_channelB_params->tunerParams.rfFreq.rfHz = frequency_B;
        reason_for_update |= sdrplay_api_Update_Tuner_Frf;
    }
    if (reason_for_update != sdrplay_api_Update_None) {
        err = sdrplay_api_Update(device.dev, sdrplay_api_Tuner_B, reason_for_update, sdrplay_api_Update_Ext1_None);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_Update(0x%08x) failed: %s\n", reason_for_update, sdrplay_api_GetErrorString(err));
            sdrplay_api_ReleaseDevice(&device);
            sdrplay_api_Close();
            exit(1);
        }
    }

    /* print settings */
    fprintf(stdout, "SerNo=%s hwVer=%d tuner=0x%02x rspDuoMode=0x%02x rspDuoSampleFreq=%.0lf\n", device.SerNo, device.hwVer, device.tuner, device.rspDuoMode, device.rspDuoSampleFreq);
    fprintf(stdout, "RX A - LO=%.0lf BW=%d If=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channelA_params->tunerParams.rfFreq.rfHz, rx_channelA_params->tunerParams.bwType, rx_channelA_params->tunerParams.ifType, rx_channelA_params->ctrlParams.decimation.decimationFactor, rx_channelA_params->ctrlParams.agc.enable, rx_channelA_params->tunerParams.gain.gRdB, rx_channelA_params->tunerParams.gain.LNAstate);
    fprintf(stdout, "RX A - DCenable=%d IQenable=%d dcCal=%d speedUp=%d trackTime=%d refreshRateTime=%d\n", (int)(rx_channelA_params->ctrlParams.dcOffset.DCenable), (int)(rx_channelA_params->ctrlParams.dcOffset.IQenable), (int)(rx_channelA_params->tunerParams.dcOffsetTuner.dcCal), (int)(rx_channelA_params->tunerParams.dcOffsetTuner.speedUp), rx_channelA_params->tunerParams.dcOffsetTuner.trackTime, rx_channelA_params->tunerParams.dcOffsetTuner.refreshRateTime);
    fprintf(stdout, "RX B - LO=%.0lf BW=%d If=%d Dec=%d IFagc=%d IFgain=%d LNAgain=%d\n", rx_channelB_params->tunerParams.rfFreq.rfHz, rx_channelB_params->tunerParams.bwType, rx_channelB_params->tunerParams.ifType, rx_channelB_params->ctrlParams.decimation.decimationFactor, rx_channelB_params->ctrlParams.agc.enable, rx_channelB_params->tunerParams.gain.gRdB, rx_channelB_params->tunerParams.gain.LNAstate);
    fprintf(stdout, "RX B - DCenable=%d IQenable=%d dcCal=%d speedUp=%d trackTime=%d refreshRateTime=%d\n", (int)(rx_channelB_params->ctrlParams.dcOffset.DCenable), (int)(rx_channelB_params->ctrlParams.dcOffset.IQenable), (int)(rx_channelB_params->tunerParams.dcOffsetTuner.dcCal), (int)(rx_channelB_params->tunerParams.dcOffsetTuner.speedUp), rx_channelB_params->tunerParams.dcOffsetTuner.trackTime, rx_channelB_params->tunerParams.dcOffsetTuner.refreshRateTime);

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
    if (rx_channelA_params->ctrlParams.decimation.enable != (decimation_A > 1)) {
        fprintf(stderr, "unexpected change - RX A decimation.enable: %d -> %d\n", decimation_A > 1, rx_channelA_params->ctrlParams.decimation.enable);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.decimation.enable != (decimation_B > 1)) {
        fprintf(stderr, "unexpected change - RX B decimation.enable: %d -> %d\n", decimation_B > 1, rx_channelB_params->ctrlParams.decimation.enable);
        init_ok = 0;
    }
    if (rx_channelA_params->ctrlParams.decimation.decimationFactor != decimation_A) {
        fprintf(stderr, "unexpected change - RX A decimation.decimationFactor: %d -> %d\n", decimation_A, rx_channelA_params->ctrlParams.decimation.decimationFactor);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.decimation.decimationFactor != decimation_B) {
        fprintf(stderr, "unexpected change - RX B decimation.decimationFactor: %d -> %d\n", decimation_B, rx_channelB_params->ctrlParams.decimation.decimationFactor);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.ifType != if_frequency_A) {
        fprintf(stderr, "unexpected change - RX A ifType: %d -> %d\n", if_frequency_A, rx_channelA_params->tunerParams.ifType);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.ifType != if_frequency_B) {
        fprintf(stderr, "unexpected change - RX B ifType: %d -> %d\n", if_frequency_B, rx_channelB_params->tunerParams.ifType);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.bwType != if_bandwidth_A) {
        fprintf(stderr, "unexpected change - RX A bwType: %d -> %d\n", if_bandwidth_A, rx_channelA_params->tunerParams.bwType);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.bwType != if_bandwidth_B) {
        fprintf(stderr, "unexpected change - RX B bwType: %d -> %d\n", if_bandwidth_B, rx_channelB_params->tunerParams.bwType);
        init_ok = 0;
    }
    if (rx_channelA_params->ctrlParams.agc.enable != agc_A) {
        fprintf(stderr, "unexpected change - RX A agc.enable: %d -> %d\n", agc_A, rx_channelA_params->ctrlParams.agc.enable);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.agc.enable != agc_B) {
        fprintf(stderr, "unexpected change - RX B agc.enable: %d -> %d\n", agc_B, rx_channelB_params->ctrlParams.agc.enable);
        init_ok = 0;
    }
    if (agc_A == sdrplay_api_AGC_DISABLE) {
        if (rx_channelA_params->tunerParams.gain.gRdB != gRdB_A) {
            fprintf(stderr, "unexpected change - RX A gain.gRdB: %d -> %d\n", gRdB_A, rx_channelA_params->tunerParams.gain.gRdB);
            init_ok = 0;
        }
    }
    if (agc_B == sdrplay_api_AGC_DISABLE) {
        if (rx_channelB_params->tunerParams.gain.gRdB != gRdB_B) {
            fprintf(stderr, "unexpected change - RX B gain.gRdB: %d -> %d\n", gRdB_B, rx_channelB_params->tunerParams.gain.gRdB);
            init_ok = 0;
        }
    }
    if (rx_channelA_params->tunerParams.gain.LNAstate != LNAstate_A) {
        fprintf(stderr, "unexpected change - RX A gain.LNAstate: %d -> %d\n", LNAstate_A, rx_channelA_params->tunerParams.gain.LNAstate);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.gain.LNAstate != LNAstate_B) {
        fprintf(stderr, "unexpected change - RX B gain.LNAstate: %d -> %d\n", LNAstate_B, rx_channelB_params->tunerParams.gain.LNAstate);
        init_ok = 0;
    }
    if (rx_channelA_params->ctrlParams.dcOffset.DCenable != DCenable_A) {
        fprintf(stderr, "unexpected change - RX A dcOffset.DCenable: %d -> %d\n", DCenable_A, rx_channelA_params->ctrlParams.dcOffset.DCenable);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.dcOffset.DCenable != DCenable_B) {
        fprintf(stderr, "unexpected change - RX B dcOffset.DCenable: %d -> %d\n", DCenable_B, rx_channelB_params->ctrlParams.dcOffset.DCenable);
        init_ok = 0;
    }
    if (rx_channelA_params->ctrlParams.dcOffset.IQenable != IQenable_A) {
        fprintf(stderr, "unexpected change - RX A dcOffset.IQenable: %d -> %d\n", IQenable_A, rx_channelA_params->ctrlParams.dcOffset.IQenable);
        init_ok = 0;
    }
    if (rx_channelB_params->ctrlParams.dcOffset.IQenable != IQenable_B) {
        fprintf(stderr, "unexpected change - RX B dcOffset.IQenable: %d -> %d\n", IQenable_B, rx_channelB_params->ctrlParams.dcOffset.IQenable);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.dcOffsetTuner.dcCal != dcCal) {
        fprintf(stderr, "unexpected change - RX A dcOffsetTuner.dcCal: %d -> %d\n", dcCal, rx_channelA_params->tunerParams.dcOffsetTuner.dcCal);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.dcOffsetTuner.speedUp != speedUp) {
        fprintf(stderr, "unexpected change - RX A dcOffsetTuner.speedUp: %d -> %d\n", speedUp, rx_channelA_params->tunerParams.dcOffsetTuner.speedUp);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.dcOffsetTuner.trackTime != trackTime) {
        fprintf(stderr, "unexpected change - RX A dcOffsetTuner.trackTime: %d -> %d\n", trackTime, rx_channelA_params->tunerParams.dcOffsetTuner.trackTime);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.dcOffsetTuner.refreshRateTime != refreshRateTime) {
        fprintf(stderr, "unexpected change - RX A dcOffsetTuner.refreshRateTime: %d -> %d\n", refreshRateTime, rx_channelA_params->tunerParams.dcOffsetTuner.refreshRateTime);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.dcOffsetTuner.dcCal != dcCal) {
        fprintf(stderr, "unexpected change - RX B dcOffsetTuner.dcCal: %d -> %d\n", dcCal, rx_channelB_params->tunerParams.dcOffsetTuner.dcCal);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.dcOffsetTuner.speedUp != speedUp) {
        fprintf(stderr, "unexpected change - RX B dcOffsetTuner.speedUp: %d -> %d\n", speedUp, rx_channelB_params->tunerParams.dcOffsetTuner.speedUp);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.dcOffsetTuner.trackTime != trackTime) {
        fprintf(stderr, "unexpected change - RX B dcOffsetTuner.trackTime: %d -> %d\n", trackTime, rx_channelB_params->tunerParams.dcOffsetTuner.trackTime);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.dcOffsetTuner.refreshRateTime != refreshRateTime) {
        fprintf(stderr, "unexpected change - RX B dcOffsetTuner.refreshRateTime: %d -> %d\n", refreshRateTime, rx_channelB_params->tunerParams.dcOffsetTuner.refreshRateTime);
        init_ok = 0;
    }
    if (rx_channelA_params->tunerParams.rfFreq.rfHz != frequency_A) {
        fprintf(stderr, "unexpected change - RX A rfHz: %.0lf -> %.0lf\n", frequency_A, rx_channelA_params->tunerParams.rfFreq.rfHz);
        init_ok = 0;
    }
    if (rx_channelB_params->tunerParams.rfFreq.rfHz != frequency_B) {
        fprintf(stderr, "unexpected change - RX B rfHz: %.0lf -> %.0lf\n", frequency_B, rx_channelB_params->tunerParams.rfFreq.rfHz);
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
          .gpio_tick = 0,
          .total_samples = 0,
          .next_sample_num = 0xffffffff,
          .output_fd = -1,
          .imin = SHRT_MAX,
          .imax = SHRT_MIN,
          .qmin = SHRT_MAX,
          .qmax = SHRT_MIN,
          .rx_id = 'A'
        },
        { .earliest_callback = {0, 0},
          .latest_callback = {0, 0},
          .gpio_tick = 0,
          .total_samples = 0,
          .next_sample_num = 0xffffffff,
          .output_fd = -1,
          .imin = SHRT_MAX,
          .imax = SHRT_MIN,
          .qmin = SHRT_MAX,
          .qmax = SHRT_MIN,
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


    pin_mask |= (1<<pin);
    const struct sched_param priority = {1};
    sched_setscheduler(0,SCHED_FIFO, &priority);


    gpioSetGetSamplesFunc(samples, pin_mask);
    usleep(1500000);
    err = sdrplay_api_Init(device.dev, &callbackFns, rx_contexts);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Init() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }
    // since sdrplay_api_Init() resets channelB settings to channelA values,
    // we need to update all the settings for channelB that are different
    reason_for_update = sdrplay_api_Update_None;
    gpioSetGetSamplesFunc(NULL,pin_mask);
    if (decimation_B != decimation_A) {
        rx_channelB_params->ctrlParams.decimation.enable = decimation_B > 1;
        rx_channelB_params->ctrlParams.decimation.decimationFactor = decimation_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_Decimation;
    }
    if (if_frequency_B != if_frequency_A) {
        rx_channelB_params->tunerParams.ifType = if_frequency_B;
        reason_for_update |= sdrplay_api_Update_Tuner_IfType;
    }
    if (if_bandwidth_B != if_bandwidth_A) {
        rx_channelB_params->tunerParams.bwType = if_bandwidth_B;
        reason_for_update |= sdrplay_api_Update_Tuner_BwType;
    }
    if (agc_B != agc_A) {
        rx_channelB_params->ctrlParams.agc.enable = agc_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_Agc;
    }
    if (agc_B == sdrplay_api_AGC_DISABLE) {
        if (gRdB_B != gRdB_A) {
            rx_channelB_params->tunerParams.gain.gRdB = gRdB_B;
            reason_for_update |= sdrplay_api_Update_Tuner_Gr;
        }
    }
    if (LNAstate_B != LNAstate_A) {
        rx_channelB_params->tunerParams.gain.LNAstate = LNAstate_B;
        reason_for_update |= sdrplay_api_Update_Tuner_Gr;
    }
    if (DCenable_B != DCenable_A) {
        rx_channelB_params->ctrlParams.dcOffset.DCenable = DCenable_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_DCoffsetIQimbalance;
    }
    if (IQenable_B != IQenable_A) {
        rx_channelB_params->ctrlParams.dcOffset.IQenable = IQenable_B;
        reason_for_update |= sdrplay_api_Update_Ctrl_DCoffsetIQimbalance;
    }
    if (frequency_B != frequency_A) {
        rx_channelB_params->tunerParams.rfFreq.rfHz = frequency_B;
        reason_for_update |= sdrplay_api_Update_Tuner_Frf;
    }
    if (reason_for_update != sdrplay_api_Update_None) {
        err = sdrplay_api_Update(device.dev, sdrplay_api_Tuner_B, reason_for_update, sdrplay_api_Update_Ext1_None);
        if (err != sdrplay_api_Success) {
            fprintf(stderr, "sdrplay_api_Update(0x%08x) failed: %s\n", reason_for_update, sdrplay_api_GetErrorString(err));
            sdrplay_api_ReleaseDevice(&device);
            sdrplay_api_Close();
            exit(1);
        }
    }

    fprintf(stderr, "streaming for %d seconds\n", streaming_time);
    sleep(streaming_time);
    

    err = sdrplay_api_Uninit(device.dev);
    sched_setscheduler(0,SCHED_OTHER,&priority);
    if (err != sdrplay_api_Success) {
        fprintf(stderr, "sdrplay_api_Uninit() failed: %s\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_ReleaseDevice(&device);
        sdrplay_api_Close();
        exit(1);
    }

    /* wait one second after sdrplay_api_Uninit() before closing the files */
    sleep(1);
    gpioTerminate();
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
        // Stop measuring time and calculate the elapsed time 
        double elapsed_to_pps = (rx_context->gpio_tick - gpio_data.latest_tick) * 1e-6;
       // long timerseconds = rx_context->earliest_callback.tv_sec - begin.tv_sec;
       // long timermicroseconds = rx_context->earliest_callback.tv_usec - begin.tv_usec;
       // double elapsed = timerseconds + timermicroseconds*1e-6;
        printf("Time measured: %f seconds.\n", elapsed_to_pps);
        double actual_sample_rate = (double)(rx_context->total_samples) / elapsed_sec;
        int rounded_sample_rate_kHz = (int)(actual_sample_rate / 1000.0 + 0.5);
        fprintf(stderr, "RX %c - total_samples=%llu actual_sample_rate=%.0lf rounded_sample_rate_kHz=%d\n", rx_context->rx_id, rx_context->total_samples, actual_sample_rate, rounded_sample_rate_kHz);
        fprintf(stderr, "RX %c - I_range=[%hd,%hd] Q_range=[%hd,%hd]\n", rx_context->rx_id, rx_context->imin, rx_context->imax, rx_context->qmin, rx_context->qmax);
        const char *samplerate_string = "SAMPLERATE";
        if (output_file != NULL && strstr(output_file, samplerate_string)) {
            char old_filename[MAX_PATH_SIZE];
            snprintf(old_filename, MAX_PATH_SIZE, output_file, rx_context->rx_id);
            char *p = strstr(old_filename, samplerate_string);
            int from = p - old_filename;
            int to = from + strlen(samplerate_string);
            char new_filename[MAX_PATH_SIZE];
            snprintf(new_filename, MAX_PATH_SIZE, "%.*s%f%s", from, old_filename, elapsed_to_pps, old_filename + to);
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
/*
static void edges(int gpio, int level, uint32_t tick)
{
    if (level ==1) 
    {
        if (first_time)
        {
            gpio_data.latest_tick = tick;
            gpio_data.pulse_count++;
            printf("edge caught, %d pulses seen\n", gpio_data.pulse_count);
            if (gpio != pin){
                fprintf(stderr, "GPIOpin doesn't match, this is no bueno");
            }
            first_time = 0;
        }
    }

}
*/

static void samples(const gpioSample_t *samples, int numSamples)
{
    static int inited = 0;

    static uint32_t lastLevel;

    uint32_t high, level;
    int s;

    for (s=0; s<numSamples; s++)
    {
       if (!inited)
       {
       inited = 1;
       lastLevel = samples[0].level;
       }

       level = samples[s].level;
       high = ((lastLevel ^ level) & pin_mask) & level;
       lastLevel = level;

       /* only interested in low to high */
       if (high)
       {
             if (high & (1<<pin)) 
             {
                gpio_data.latest_tick = samples[s].tick;
                gpio_data.pulse_count++;
                
             }
       }
    }   

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
    fprintf(stderr, "    -D disable post tuner DC offset compensation (default: enabled)\n");
    fprintf(stderr, "    -I disable post tuner I/Q balance compensation (default: enabled)\n");
    fprintf(stderr, "    -y tuner DC offset compensation parameters <dcCal,speedUp,trackTime,refeshRateTime> (default: 3,0,1,2048)\n");
    fprintf(stderr, "    -f <center frequency>\n");
    fprintf(stderr, "    -x <streaming time (s)> (default: 10s)\n");
    fprintf(stderr, "    -o <output file> ('%%c' will be replaced by the channel id (A or B) and 'SAMPLERATE' will be replaced by the estimated sample rate in kHz)\n");
    fprintf(stderr, "    -L enable SDRplay API debug log level (default: disabled)\n");
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
    uint32_t tick = gpioTick();
    UNUSED(reset);
    
    /* track callback timestamp */
    
    gettimeofday(&rxContext->latest_callback, NULL);
    if (rxContext->earliest_callback.tv_sec == 0) {
        rxContext->gpio_tick = tick;
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

    short imin = SHRT_MAX;
    short imax = SHRT_MIN;
    short qmin = SHRT_MAX;
    short qmax = SHRT_MIN;
    for (unsigned int i = 0; i < numSamples; i++) {
        imin = imin < xi[i] ? imin : xi[i];
        imax = imax > xi[i] ? imax : xi[i];
    }
    for (unsigned int i = 0; i < numSamples; i++) {
        qmin = qmin < xq[i] ? qmin : xq[i];
        qmax = qmax > xq[i] ? qmax : xq[i];
    }
    rxContext->imin = rxContext->imin < imin ? rxContext->imin : imin;
    rxContext->imax = rxContext->imax > imax ? rxContext->imax : imax;
    rxContext->qmin = rxContext->qmin < qmin ? rxContext->qmin : qmin;
    rxContext->qmax = rxContext->qmax > qmax ? rxContext->qmax : qmax;
    

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
