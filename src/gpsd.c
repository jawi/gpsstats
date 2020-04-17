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

    timestamp_t time;
    int sats_visible;
    int sats_used;
    long qErr;
    double tdop;
    double avg_snr;
    struct timespec toff_diff;
    struct timespec pps_diff;
    uint8_t sats_seen[GNSSID_CNT];

    uint32_t gpsd_events_recv;
    uint32_t gpsd_events_send;
    time_t gpsd_last_event;
};

static bool gps_stats_changed(gpsd_handle_t *handle, struct gps_data_t *data) {
    double snr_total = 0;
    double snr_avg = 0;
    uint8_t sats_seen[GNSSID_CNT] = { 0 };

    for(int i = 0; i <= data->satellites_visible; i++) {
        if (!data->skyview[i].used) {
            continue;
        }

        if (data->skyview[i].ss > 1) {
            snr_total += data->skyview[i].ss;
        }

        if (data->skyview[i].svid != 0) {
            uint8_t gnssid = data->skyview[i].gnssid;
            if (gnssid < GNSSID_CNT) {
                sats_seen[gnssid]++;
            }
        }
    }
    if (data->satellites_used > 0) {
        snr_avg = snr_total / data->satellites_used;
    }

    if ((handle->sats_used == data->satellites_used) &&
            (handle->sats_visible == data->satellites_visible) &&
            (handle->tdop == data->dop.tdop) &&
            (handle->avg_snr == snr_avg) &&
            memcmp(handle->sats_seen, sats_seen, sizeof(sats_seen)) == 0) {
        return false;
    }

    // Update our local stats...
    handle->time = data->fix.time;
    handle->qErr = data->fix.qErr;
    handle->sats_used = data->satellites_used;
    handle->sats_visible = data->satellites_visible;
    handle->tdop = data->dop.tdop;
    handle->avg_snr = snr_avg;
    memcpy(handle->sats_seen, sats_seen, sizeof(sats_seen));

    return true;
}

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
    size_t offset = 0;
    size_t buffer_size = INITIAL_BUFFER_SIZE;

    *buffer = malloc(buffer_size * sizeof(char));

    BUFFER_ADD("{\"time\":%.0f,\"sats_used\":%d,\"sats_visible\":%d,\"tdop\":%f,\"avg_snr\":%f",
               handle->time,
               handle->sats_used,
               handle->sats_visible,
               handle->tdop,
               handle->avg_snr);

    if (handle->qErr) {
        BUFFER_ADD(",\"qErr\":%ld", handle->qErr);
    }

    BUFFER_ADD(",\"toff\":%f", TSTONS(&handle->toff_diff));
    BUFFER_ADD(",\"pps\":%f", TSTONS(&handle->pps_diff));

    for (int i = 0; i < GNSSID_CNT; i++) {
        uint8_t seen = handle->sats_seen[i];
        if (seen > 0) {
            BUFFER_ADD(",\"sats.%s\":%d", gnssid_name[i], handle->sats_seen[i]);
        }
    }

    BUFFER_ADD("}");

    return (int) offset;
}

int gpsd_read_data(gpsd_handle_t *handle, char **result) {
    if (handle == NULL) {
        return -EINVAL;
    }

    int status = gps_read(&handle->gpsd, NULL, 0);
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
        log_debug("Connected to GPSD with protocol v%d.%d (release local: %s, rev: %s)",
                  handle->gpsd.version.proto_major,
                  handle->gpsd.version.proto_minor,
                  handle->gpsd.version.release,
                  handle->gpsd.version.rev);
    }

    if (handle->gpsd.set & TOFF_SET) {
        TS_SUB(&handle->toff_diff, &handle->gpsd.toff.clock, &handle->gpsd.toff.real);
    }

    if (handle->gpsd.set & PPS_SET) {
        TS_SUB(&handle->pps_diff, &handle->gpsd.pps.clock, &handle->gpsd.pps.real);
    }

    if ((handle->gpsd.fix.mode > MODE_NO_FIX) &&
            (handle->gpsd.satellites_used > 0) &&
            gps_stats_changed(handle, &handle->gpsd)) {
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