/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <udaemon/udaemon.h>

#include <gps.h>

#include "gpsd.h"
#include "timespec.h"

#define GPSD_ERROR(s) \
    ((errno) ? strerror(errno) : gps_errstr(s))

#ifndef GNSSID_CNT
/* copied from gpsd-3.17: defines for u-blox gnssId, as used in satellite_t */
#define GNSSID_GPS 0
#define GNSSID_SBAS 1
#define GNSSID_GAL 2
#define GNSSID_BD 3
#define GNSSID_IMES 4
#define GNSSID_QZSS 5
#define GNSSID_GLO 6
#define GNSSID_IRNSS 7            /* Not defined by u-blox */
#define GNSSID_CNT 8              /* count for array size */
#endif

static const char* gnssid_name[GNSSID_CNT] = {
    "gps",
    "sbas",
    "galileo",
    "beidou",
    "imes",
    "qzss",
    "glonass",
    "irnss"
};

struct gpsd_handle {
    struct gps_data_t gpsd;
    char *host;
    char *port;
    char *device;

    struct timespec toff_diff;
    struct timespec pps_diff;

    uint32_t gpsd_events_recv;
    uint32_t gpsd_events_send;
    time_t gpsd_last_event;
};

gpsd_handle_t *gpsd_init(const config_t *config) {
    gpsd_handle_t *handle = malloc(sizeof(gpsd_handle_t));
    if (handle == NULL) {
        log_error("failed to create GPSD handle: out of memory!");
        return NULL;
    }
    bzero(handle, sizeof(gpsd_handle_t));

    handle->host = config->gpsd_host;
    handle->port = config->gpsd_port;
    handle->device = config->gpsd_device;

    return handle;
}

void gpsd_destroy(gpsd_handle_t *handle) {
    if (handle) {
        free(handle);
    }
}

int gpsd_connect(gpsd_handle_t *handle) {
    if (handle == NULL) {
        return -EINVAL;
    }

    unsigned int flags = WATCH_ENABLE | WATCH_NEWSTYLE | WATCH_JSON | WATCH_PPS | WATCH_TIMING;

    int status;
    if ((status = gps_open(handle->host, handle->port, &handle->gpsd)) < 0) {
        log_error("no GPSD running or network error: %s", GPSD_ERROR(status));
        return -ENOTCONN;
    }

    if (handle->device) {
        flags |= WATCH_DEVICE;
    }

    if ((status = gps_stream(&handle->gpsd, flags, handle->device)) < 0) {
        log_error("failed to set GPS stream options: %s", GPSD_ERROR(status));
        return -ENOTCONN;
    }

    log_info("connected to GPSD...");

    return 0;
}

int gpsd_disconnect(gpsd_handle_t *handle) {
    if (handle == NULL) {
        return -EINVAL;
    }
    int fd = (handle->gpsd).gps_fd;
    if (fd < 0) {
        return 0;
    }

    int status;

    // Best effort: try to clean up, but do not fail...
    if ((status = gps_stream(&handle->gpsd, WATCH_DISABLE, NULL)) < 0) {
        log_debug("Failed to close stream to GPSD: %s", GPSD_ERROR(status));
    }
    if ((status = gps_close(&handle->gpsd)) < 0) {
        log_debug("Failed to close handle to GPSD: %s", GPSD_ERROR(status));
    }

    log_info("disconnected from GPSD...");

    return 0;
}

int gpsd_fd(gpsd_handle_t *handle) {
    if (handle == NULL) {
        return -EINVAL;
    }

    int fd = (handle->gpsd).gps_fd;
    if (fd < 0) {
        log_error("Failed to obtain GPSD file descriptor!");
    }
    return fd;
}

#define INITIAL_BUFFER_SIZE 256

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    int status;                                                                \
    status = snprintf(*buffer + offset, buffer_size - offset, __VA_ARGS__);     \
    if (status < 1) {                                                          \
      free(*buffer);                                                            \
      return -ENOMEM;                                                             \
    } else if (((size_t)status) >= (buffer_size - offset)) {                   \
      free(*buffer);                                                            \
      return -ENOMEM;                                                             \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

static int create_event_payload(gpsd_handle_t *handle, char **buffer) {
    double snr_total = 0;
    double snr_avg = 0;
    uint8_t sats_seen[GNSSID_CNT] = { 0 };

    for(int i = 0; i <= handle->gpsd.satellites_visible; i++) {
        if (!handle->gpsd.skyview[i].used) {
            continue;
        }

        if (handle->gpsd.skyview[i].ss >= 0) {
            snr_total += handle->gpsd.skyview[i].ss;
        }

#if GPSD_API_MAJOR_VERSION >= 8
        if (handle->gpsd.skyview[i].svid != 0) {
            uint8_t gnssid = handle->gpsd.skyview[i].gnssid;
            if (gnssid < GNSSID_CNT) {
                sats_seen[gnssid]++;
            }
        }
#else
        int gnssid = -1;
        short prn = handle->gpsd.skyview[i].PRN;

        if (GPS_PRN(prn)) {
            gnssid = GNSSID_GPS;
        } else if (GBAS_PRN(prn)) {
            gnssid = GNSSID_GLO;
        } else if (SBAS_PRN(prn)) {
            gnssid = GNSSID_SBAS;
        } else if (GNSS_PRN(prn)) {
            gnssid = GNSSID_BD;
        }
        if (gnssid >= 0 && gnssid < GNSSID_CNT) {
            sats_seen[gnssid]++;
        }
#endif
    }
    if (handle->gpsd.satellites_used > 0) {
        snr_avg = snr_total / handle->gpsd.satellites_used;
    }

    size_t offset = 0;
    size_t buffer_size = INITIAL_BUFFER_SIZE;

    *buffer = malloc(buffer_size * sizeof(char));

    BUFFER_ADD("{");

#if GPSD_API_MAJOR_VERSION >= 9
    BUFFER_ADD("\"time\":%ld.%.9ld", handle->gpsd.fix.time.tv_sec, handle->gpsd.fix.time.tv_nsec);
#elif GPSD_API_MAJOR_VERSION >= 8
    BUFFER_ADD("\"time\":%.9f", handle->gpsd.fix.time);
#endif

    BUFFER_ADD(",\"sats_used\":%d,\"sats_visible\":%d,\"tdop\":%f,\"avg_snr\":%f",
               handle->gpsd.satellites_used,
               handle->gpsd.satellites_visible,
               handle->gpsd.dop.tdop,
               snr_avg);

    long qErr = 0;
#if GPSD_API_MAJOR_VERSION >= 9
    qErr = handle->gpsd.qErr;
#elif GPSD_API_MAJOR_VERSION >= 8
    qErr = data->fix.qErr;
#endif

    if (qErr != 0) {
        BUFFER_ADD(",\"qErr\":%ld", qErr);
    }

    BUFFER_ADD(",\"toff\":%f", TSTONS(&handle->toff_diff));
    BUFFER_ADD(",\"pps\":%f", TSTONS(&handle->pps_diff));

    if (handle->gpsd.osc.running) {
        if (handle->gpsd.osc.reference) {
            BUFFER_ADD(",\"osc.pps\":true");
        } else {
            BUFFER_ADD(",\"osc.pps\":false");
        }
        if (handle->gpsd.osc.disciplined) {
            BUFFER_ADD(",\"osc.gps\":true");
        } else {
            BUFFER_ADD(",\"osc.gps\":false");
        }
        BUFFER_ADD(",\"osc.delta\":%d", handle->gpsd.osc.delta);
    }

    for (int i = 0; i < GNSSID_CNT; i++) {
        uint8_t seen = sats_seen[i];
        if (seen > 0) {
            BUFFER_ADD(",\"sats.%s\":%d", gnssid_name[i], sats_seen[i]);
        }
    }

    BUFFER_ADD("}");

    return (int) offset;
}

int gpsd_read_data(gpsd_handle_t *handle, char **result) {
    if (handle == NULL) {
        return -EINVAL;
    }

#if GPSD_API_MAJOR_VERSION >= 8
    int status = gps_read(&handle->gpsd, NULL, 0);
#else
    int status = gps_read(&handle->gpsd);
#endif
    if (status < 0) {
        log_warning("Failed to read from GPSD: %s", GPSD_ERROR(status));
        return -ENOTCONN;
    } else if (status == 0) {
        // No data was available...
        return 0;
    }

    if (handle->gpsd.set & ERROR_SET) {
        log_warning("GPSD returned error: %s", handle->gpsd.error);
        return -EIO;
    }

    // Update stats...
    handle->gpsd_events_recv++;
    handle->gpsd_last_event = time(NULL);

    if (handle->gpsd.set & VERSION_SET) {
        log_info("Connected to GPSD with protocol v%d.%d (release: %s)",
                  handle->gpsd.version.proto_major,
                  handle->gpsd.version.proto_minor,
                  handle->gpsd.version.release);
    }

    if (!(handle->gpsd.set & PACKET_SET)) {
        // nothing of interest...
        return 0;
    }


#if GPSD_API_MAJOR_VERSION >= 9
    TS_SUB(&handle->toff_diff, &handle->gpsd.toff.clock, &handle->gpsd.toff.real);
    TS_SUB(&handle->pps_diff, &handle->gpsd.pps.clock, &handle->gpsd.pps.real);
#else
    if (handle->gpsd.set & TOFF_SET) {
        TS_SUB(&handle->toff_diff, &handle->gpsd.toff.clock, &handle->gpsd.toff.real);
    }

    if (handle->gpsd.set & PPS_SET) {
        TS_SUB(&handle->pps_diff, &handle->gpsd.pps.clock, &handle->gpsd.pps.real);
    }
#endif

    handle->gpsd.set = 0;

    if ((handle->gpsd.fix.mode > MODE_NO_FIX) && (handle->gpsd.satellites_used > 0)) {
        // Update stats...
        handle->gpsd_events_send++;

        return create_event_payload(handle, result);
    }

    return 0;
}

gpsd_stats_t gpsd_dump_stats(gpsd_handle_t *handle) {
    if (handle == NULL) {
        return (gpsd_stats_t) {
            0
        };
    }

    return (gpsd_stats_t) {
        .events_recv = handle->gpsd_events_recv,
        .events_send = handle->gpsd_events_send,
        .last_event = handle->gpsd_last_event,
    };
}

// EOF