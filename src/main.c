/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2019 jawi
 *   License: Apache License 2.0
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gps.h>
#include <mosquitto.h>

#include "config.h"
#include "gpsstats.h"
#include "logging.h"
#include "timespec.h"
#include "util.h"

#define MAX_RECONNECT_DELAY_VALUE 32

#define MOSQ_ERROR(s) \
	((s) == MOSQ_ERR_ERRNO) ? strerror(errno) : mosquitto_strerror((s))

typedef struct {
    config_t *config;
    struct mosquitto *mosq;

    bool reconnect_mosq;
    time_t next_reconnect_attempt;
    uint32_t next_delay_value;

    timestamp_t time;
    int sats_visible;
    int sats_used;
    long qErr;
    double tdop;
    double avg_snr;
    struct timespec toff_diff;
    struct timespec pps_diff;
    uint8_t sats_seen[GNSSID_CNT];
    bool loop;
} run_state_t;

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

static run_state_t run_state = { 0 };

static void quit_handler(int signum) {
    run_state.loop = false;
}

static bool gps_stats_changed(struct gps_data_t *data) {
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

    if ((run_state.sats_used == data->satellites_used) &&
            (run_state.sats_visible == data->satellites_visible) &&
            (run_state.tdop == data->dop.tdop) &&
            (run_state.avg_snr == snr_avg) &&
            memcmp(run_state.sats_seen, sats_seen, sizeof(sats_seen)) == 0) {
        return false;
    }

    // Update our local stats...
    run_state.time = data->fix.time;
    run_state.qErr = data->fix.qErr;
    run_state.sats_used = data->satellites_used;
    run_state.sats_visible = data->satellites_visible;
    run_state.tdop = data->dop.tdop;
    run_state.avg_snr = snr_avg;
    memcpy(run_state.sats_seen, sats_seen, sizeof(sats_seen));

    return true;
}

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

static char *create_mqtt_payload(void) {
    size_t offset = 0;
    size_t buffer_size = INITIAL_BUFFER_SIZE;
    char *buffer = malloc(buffer_size * sizeof(char));

    BUFFER_ADD("{\"time\":%f,\"sats_used\":%d,\"sats_visible\":%d,\"tdop\":%f,\"avg_snr\":%f",
               run_state.time,
               run_state.sats_used,
               run_state.sats_visible,
               run_state.tdop,
               run_state.avg_snr);

    if (run_state.qErr) {
        BUFFER_ADD(",\"qErr\":%ld", run_state.qErr);
    }

    BUFFER_ADD(",\"toff\":%f", TSTONS(&run_state.toff_diff));
    BUFFER_ADD(",\"pps\":%f", TSTONS(&run_state.pps_diff));

    for (int i = 0; i < GNSSID_CNT; i++) {
        uint8_t seen = run_state.sats_seen[i];
        if (seen > 0) {
            BUFFER_ADD(",\"sats.%s\":%d", gnssid_name[i], run_state.sats_seen[i]);
        }
    }

    BUFFER_ADD("}");

    return buffer;
}

static void send_mqtt_event(const char *event_data) {
    log_debug("Publishing event %s", event_data);

    int status = mosquitto_publish(run_state.mosq, NULL /* message id */,
                                   "gpsstats",
                                   (int) strlen(event_data), event_data,
                                   run_state.config->qos,
                                   run_state.config->retain);
    if (status) {
        log_warning("Failed to publish data to MQTT broker. Reason: %s", MOSQ_ERROR(status));
    }
}

static bool mqtt_needs_to_reconnect(int status) {
    return status == MOSQ_ERR_NO_CONN ||
           status == MOSQ_ERR_CONN_REFUSED ||
           status == MOSQ_ERR_CONN_LOST ||
           status == MOSQ_ERR_TLS ||
           status == MOSQ_ERR_AUTH ||
           status == MOSQ_ERR_UNKNOWN;
}

static void my_connect_cb(struct mosquitto *mosq, void *user_data, int result) {
    (void)mosq;
    (void)user_data;

    if (result) {
        log_warning("unable to connect to MQTT broker. Reason: %s", MOSQ_ERROR(result));
    } else {
        log_info("successfully connected to MQTT broker");
    }
}

static void my_disconnect_cb(struct mosquitto *mosq, void *user_data, int result) {
    (void)mosq;
    (void)user_data;

    if (result) {
        log_info("disconnected from MQTT broker. Reason: %s", MOSQ_ERROR(result));
        if (mqtt_needs_to_reconnect(result)) {
            run_state.reconnect_mosq = true;
        }
    } else {
        log_info("disconnected from MQTT broker.");
    }
}

static void my_log_callback(struct mosquitto *mosq, void *user_data, int level, const char *msg) {
    (void)mosq;
    (void)user_data;
    (void)level;
    log_debug(msg);
}

static int reconnect_mqtt(void) {
    time_t now = time(NULL);
    if (run_state.reconnect_mosq && run_state.next_reconnect_attempt >= now) {
        return 1;
    }

    run_state.reconnect_mosq = true;
    int status = mosquitto_reconnect(run_state.mosq);
    if (status != MOSQ_ERR_SUCCESS) {
        log_debug("failed to reconnect to MQTT broker: %s", MOSQ_ERROR(status));

        run_state.next_reconnect_attempt = now + run_state.next_delay_value;

        if (run_state.next_delay_value < MAX_RECONNECT_DELAY_VALUE) {
            run_state.next_delay_value <<= 1;
        }

        return -1;
    }

    run_state.reconnect_mosq = false;
    run_state.next_reconnect_attempt = -1L;
    run_state.next_delay_value = 1;

    return 0;
}

static void dump_config(config_t *config) {
    log_debug("Using configuration:");
    log_debug("- daemon user/group: %d/%d", config->priv_user, config->priv_group);
    log_debug("- GPSD server: %s:%s", config->gpsd_host, config->gpsd_port);
    if (config->gpsd_device) {
        log_debug("  - device: %s", config->gpsd_device);
    }
    log_debug("- MQTT server: %s:%d", config->mqtt_host, config->mqtt_port);
    log_debug("  - client ID: %s", config->client_id);
    log_debug("  - MQTT QoS: %d", config->qos);
    log_debug("  - retain messages: %s", config->retain ? "yes" : "no");
    if (config->use_auth) {
        log_debug("  - using client credentials");
    }
    if (config->use_tls) {
        log_debug("- using TLS options:");
        log_debug("  - use TLS version: %s", config->tls_version);
        if (config->cacertpath) {
            log_debug("  - CA cert path: %s", config->cacertpath);
        }
        if (config->cacertfile) {
            log_debug("  - CA cert file: %s", config->cacertfile);
        }
        if (config->certfile) {
            log_debug("  - using client certificate: %s", config->certfile);
        }
        log_debug("  - verify peer: %s", config->verify_peer ? "yes" : "no");
        if (config->ciphers) {
            log_debug("  - cipher suite: %s", config->ciphers);
        }
    }
}

int main(int argc, char *argv[]) {
    int timeout = 250; // milliseconds
    int status;
    unsigned int flags = WATCH_ENABLE | WATCH_NEWSTYLE | WATCH_JSON | WATCH_PPS | WATCH_TIMING;
    struct gps_data_t data = { 0 };

    // parse arguments...
    char *conf_file = NULL;
    char *pid_file = NULL;

    bool foreground = false;
    bool debug = false;
    int opt;

    while ((opt = getopt(argc, argv, "c:dfhp:v")) != -1) {
        switch (opt) {
        case 'c':
            conf_file = strdup(optarg);
            break;
        case 'd':
            debug = true;
            break;
        case 'f':
            foreground = true;
            break;
        case 'p':
            pid_file = strdup(optarg);
            break;
        case 'v':
        case 'h':
        default:
            fprintf(stderr, PROGNAME " v" VERSION "\n");
            if (opt == 'v') {
                exit(0);
            }
            fprintf(stderr, "Usage: %s [-d] [-f] [-c config file] [-p pid file] [-v]\n", PROGNAME);
            exit(1);
        }
    }

    if (!conf_file) {
        conf_file = strdup(CONF_FILE);
    }
    if (!pid_file) {
        pid_file = strdup(PID_FILE);
    }

    // close any file descriptors we inherited...
    const long max_fd = sysconf(_SC_OPEN_MAX);
    for (int fd = 3; fd < max_fd; fd++) {
        close(fd);
    }
    // do this *after* we've closed the file descriptors!
    init_logging(debug, foreground);

    /* catch all interesting signals */
    (void)signal(SIGTERM, quit_handler);
    (void)signal(SIGQUIT, quit_handler);
    (void)signal(SIGINT, quit_handler);

    run_state.config = read_config(conf_file);

    // Sanity check; make sure we've got a valid configuration at hand...
    if (run_state.config == NULL) {
        goto cleanup;
    }

    dump_config(run_state.config);

    if (!foreground) {
        int retval = daemonize(pid_file, run_state.config->priv_user, run_state.config->priv_group);
        if (retval) {
            exit(retval);
        }
    }

    run_state.mosq = mosquitto_new(run_state.config->client_id, true /* clean session */, NULL);
    if (!run_state.mosq) {
        log_error("failed to create new mosquitto instance");
        goto cleanup;
    }

    // TODO: set TLS & auth parameters to mosquitto...

    mosquitto_connect_callback_set(run_state.mosq, my_connect_cb);
    mosquitto_disconnect_callback_set(run_state.mosq, my_disconnect_cb);
    mosquitto_log_callback_set(run_state.mosq, my_log_callback);

    if (gps_open(run_state.config->gpsd_host, run_state.config->gpsd_port, &data)) {
        log_error("no gpsd running or network error: %d, %s\n", errno, gps_errstr(errno));
        exit(EXIT_FAILURE);
    }

    if (run_state.config->gpsd_device) {
        flags |= WATCH_DEVICE;
    }

    if (gps_stream(&data, flags, run_state.config->gpsd_device)) {
        log_error("failed to set GPS stream options: %d, %s\n", errno, gps_errstr(errno));
        exit(EXIT_FAILURE);
    }

    status = mosquitto_connect(run_state.mosq, run_state.config->mqtt_host, run_state.config->mqtt_port, 60 /* keepalive */);
    if (status) {
        log_warning("failed to connect to MQTT broker: %s", MOSQ_ERROR(status));
    }

    run_state.loop = true;

    while (run_state.loop) {
        if (run_state.reconnect_mosq) {
            reconnect_mqtt();
        }

        mosquitto_loop(run_state.mosq, timeout, 1 /* max_packets */);

        if (gps_waiting(&data, timeout)) {
            if (gps_read(&data, NULL, 0) < 0) {
                log_warning("failed to read data from GPSD!");
                continue;
            }

            if (data.set & VERSION_SET) {
                log_debug("Connected to GPSD with protocol v%d.%d", data.version.proto_major, data.version.proto_minor);
            }

            if (data.set & ERROR_SET) {
                log_warning("GPSD returned: %s", data.error);
                continue;
            }

            if (data.set & TOFF_SET) {
                TS_SUB(&run_state.toff_diff, &data.toff.clock, &data.toff.real);
            }

            if (data.set & PPS_SET) {
                TS_SUB(&run_state.pps_diff, &data.pps.clock, &data.pps.real);
            }

            if ((data.fix.mode > MODE_NO_FIX) && (data.satellites_used > 0) && gps_stats_changed(&data)) {
                char *event_data = create_mqtt_payload();
                if (event_data) {
                    send_mqtt_event(event_data);
                    free(event_data);
                }
            }
        }
    }

cleanup:
    log_debug("Closing connection to GPSD...");
    gps_stream(&data, WATCH_DISABLE, NULL);
    gps_close(&data);

    log_debug("Closing connection to MQTT...");
    mosquitto_disconnect(run_state.mosq);
    mosquitto_destroy(run_state.mosq);
    mosquitto_lib_cleanup();

    destroy_logging();
    free_config(run_state.config);

    // best effort; will only succeed if the permissions are set correctly...
    unlink(pid_file);

    free(conf_file);
    free(pid_file);

    return 0;
}