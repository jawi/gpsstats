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

#include <udaemon/udaemon.h>

#include <gps.h>

#include "gpsd.h"
#include "timespec.h"

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
    struct gps_data_t *gpsd;
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

    time_t next_reconnect_attempt;
    int next_delay_value;
};

#define INITIAL_BUFFER_SIZE 256

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    int status;                                                                \
    status = snprintf(buffer + offset, buffer_size - offset, __VA_ARGS__);     \
    if (status < 1) {                                                          \
      free(buffer);                                                            \
      return NULL;                                                             \
    } else if (((size_t)status) >= (buffer_size - offset)) {                   \
      free(buffer);                                                            \
      return NULL;                                                             \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

static char *create_event_payload(gpsd_handle_t *handle) {
    size_t offset = 0;
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    char *buffer = malloc(buffer_size * sizeof(char));

    BUFFER_ADD("{\"time\":%f,\"sats_used\":%d,\"sats_visible\":%d,\"tdop\":%f,\"avg_snr\":%f",
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

    return buffer;
}

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
            if (gnssid >= 0 && gnssid < GNSSID_CNT) {
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
    if (!handle) {
        log_error("failed to create GPSD handle: out of memory!");
        goto err_cleanup;
    }
    bzero(handle, sizeof(gpsd_handle_t));

    struct gps_data_t *gpsd = malloc(sizeof(struct gps_data_t));
    if (gpsd == NULL) {
        log_error("failed to create new GPSD instance");
        goto err_cleanup;
    }

    handle->gpsd = gpsd;
    handle->host = config->gpsd_host;
    handle->port = config->gpsd_port;
    handle->device = config->gpsd_device;

    handle->next_reconnect_attempt = -1L;
    handle->next_delay_value = 1;

    return handle;

err_cleanup:
    if (handle) {
        free(handle);
    }
    return NULL;
}

void gpsd_destroy(gpsd_handle_t *handle) {
    // TODO
}

int gpsd_connect(gpsd_handle_t *handle) {
    unsigned int flags = WATCH_ENABLE | WATCH_NEWSTYLE | WATCH_JSON | WATCH_PPS | WATCH_TIMING;

    if (gps_open(handle->host, handle->port, handle->gpsd)) {
        log_error("no gpsd running or network error: %d, %s", errno, gps_errstr(errno));
        return -1;
    }

    if (handle->device) {
        flags |= WATCH_DEVICE;
    }

    if (gps_stream(handle->gpsd, flags, handle->device)) {
        log_error("failed to set GPS stream options: %d, %s", errno, gps_errstr(errno));
        return -1;
    }

    return 0;
}

int gpsd_disconnect(gpsd_handle_t *handle) {
    gps_stream(handle->gpsd, WATCH_DISABLE, NULL);
    gps_close(handle->gpsd);

    return 0;
}

int gpsd_fd(gpsd_handle_t *handle) {
    if (handle == NULL || handle->gpsd == NULL) {
        return -1;
    }

    int fd = handle->gpsd->gps_fd;
    if (fd < 0) {
        log_error("Failed to obtain GPSD file descriptor!");
    }
    return fd;
}

char *gpsd_read_data(gpsd_handle_t *handle) {
    int status = gps_read(handle->gpsd, NULL, 0);
    if (status < 0) {
        log_warning("Failed to read from GPSD: %d, %s", errno, gps_errstr(errno));
        return NULL;
    } else if (status == 0) {
        // No data was read?!
        return NULL;
    }

    struct gps_data_t data = *handle->gpsd;

    if (data.set & VERSION_SET) {
        log_debug("Connected to GPSD with protocol v%d.%d", data.version.proto_major, data.version.proto_minor);
    }

    if (data.set & ERROR_SET) {
        log_warning("GPSD returned: %s", data.error);
        return NULL;
    }

    if (data.set & TOFF_SET) {
        TS_SUB(&handle->toff_diff, &data.toff.clock, &data.toff.real);
    }

    if (data.set & PPS_SET) {
        TS_SUB(&handle->pps_diff, &data.pps.clock, &data.pps.real);
    }

    if ((data.fix.mode > MODE_NO_FIX) && (data.satellites_used > 0) && gps_stats_changed(handle, &data)) {
        return create_event_payload(handle);
    }

    return NULL;
}

// EOF