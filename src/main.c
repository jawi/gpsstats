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

#include <udaemon/udaemon.h>

#include "config.h"
#include "gpsd.h"
#include "gpsstats.h"
#include "mqtt.h"

typedef struct {
    char *conf_file;
    config_t *config;

    mqtt_handle_t *mqtt;
    gpsd_handle_t *gpsd;

    eh_id_t gpsd_event_handler_id;
    eh_id_t mosq_event_handler_id;
} run_state_t;

static run_state_t run_state = {
    .conf_file = CONF_FILE,
};

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

// Called when data of gpsd is received...
static void gpsstats_gps_callback(ud_state_t *ud_state, struct pollfd *pollfd) {
    if (pollfd->revents & POLLIN) {
        char *event = gpsd_read_data(run_state.gpsd);
        if (event) {
            mqtt_send_event(run_state.mqtt, event);
            free(event);
        }
    }

    // TODO: Handle reconnects...
}

// Called when data of mosquitto is received/to be transmitted...
static void gpsstats_mosquitto_callback(ud_state_t *ud_state, struct pollfd *pollfd) {
    if (pollfd->revents & POLLOUT) {
        // We can write safely...
        mqtt_write_data(run_state.mqtt);
    }
    if (pollfd->revents & POLLIN) {
        // We can read safely...
        mqtt_read_data(run_state.mqtt);
    }

    // TODO: handle reconnects!!!
}

// Initializes GPSStats
static int gpsstats_init(ud_state_t *ud_state) {
    run_state.config = read_config(run_state.conf_file);

    // Sanity check; make sure we've got a valid configuration at hand...
    if (run_state.config == NULL) {
        return -EINVAL;
    }

    dump_config(run_state.config);

    int fd;

    run_state.gpsd = gpsd_init(run_state.config);
    if (run_state.gpsd == NULL) {
        log_warning("Unable to initialize GPSD!");
    }

    if (gpsd_connect(run_state.gpsd)) {
        log_warning("Unable to connect to GPSD!");
    }

    fd = gpsd_fd(run_state.gpsd);
    if (fd) {
        if (ud_add_event_handler(ud_state, fd, POLLIN,
                                 gpsstats_gps_callback,
                                 &run_state.gpsd_event_handler_id)) {
            log_warning("Unable to add GPSD event handler!");
            return -EINVAL;
        }
    }

    run_state.mqtt = mqtt_init(run_state.config);
    if (run_state.mqtt == NULL) {
        log_warning("Unable to connect to MQTT!");
    }

    if (mqtt_connect(run_state.mqtt)) {
        log_warning("Unable to connect to MQTT!");
    }

    fd = mqtt_fd(run_state.mqtt);
    if (fd) {
        if (ud_add_event_handler(ud_state, fd, POLLIN | POLLOUT,
                                 gpsstats_mosquitto_callback,
                                 &run_state.mosq_event_handler_id)) {
            log_warning("Unable to add MQTT event handler!");
            return -EINVAL;
        }
    }

    return 0;
}

static void gpsstats_signal_handler(ud_state_t *ud_state, ud_signal_t signal) {
    if (signal == SIG_HUP) {
        // reload configuration...
        log_info("Reloading configuration file...");
    } else if (signal == SIG_USR1) {
        // dump statistics...
        log_info("Dumping statistics...");
    }
}

static int gpsstats_mqtt_misc_loop(ud_state_t *ud_state, int interval, void *context) {
    log_debug("Running misc loop...");
    mqtt_misc_loop(run_state.mqtt);
    return interval;
}

// Cleans up all resources...
static int gpsstats_cleanup(ud_state_t *ud_state) {
    log_debug("Closing connection to GPSD...");
    gpsd_disconnect(run_state.gpsd);
    gpsd_destroy(run_state.gpsd);

    log_debug("Closing connection to MQTT...");
    mqtt_disconnect(run_state.mqtt);
    mqtt_destroy(run_state.mqtt);

    free_config(run_state.config);

    if (run_state.conf_file != CONF_FILE) {
        free(run_state.conf_file);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    ud_config_t daemon_config = {
        .progname = PROGNAME,
        .pid_file = PID_FILE,
        .initialize = gpsstats_init,
        .signal_handler = gpsstats_signal_handler,
        .cleanup = gpsstats_cleanup,
    };

    // parse arguments...
    int opt;
    while ((opt = getopt(argc, argv, "c:dfhp:v")) != -1) {
        switch (opt) {
        case 'c':
            run_state.conf_file = strdup(optarg);
            break;
        case 'd':
            daemon_config.debug = true;
            break;
        case 'f':
            daemon_config.foreground = true;
            break;
        case 'p':
            daemon_config.pid_file = strdup(optarg);
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

    ud_state_t *daemon = ud_init(&daemon_config);
    // Perform periodic tasks...
    ud_schedule_task(daemon, 5, gpsstats_mqtt_misc_loop, NULL);

    int retval = ud_main_loop(daemon);

    ud_destroy(daemon);

    if (daemon_config.pid_file && daemon_config.pid_file != PID_FILE) {
        free(daemon_config.pid_file);
    }

    return retval;
    /*


        run_state.loop = true;

        while (run_state.loop) {
            if (run_state.reconnect_mosq) {
                reconnect_mqtt();
            }
            if (run_state.reconnect_gpsd) {
                reconnect_gpsd();
            }

            // 1 == max_packets
            mosquitto_loop(run_state.mosq, timeout, 1);

            if (gps_waiting(run_state.gpsd, timeout)) {
                if (gps_read(run_state.gpsd, NULL, 0) < 0) {
                    if ((gps_error_cnt++ % 10) == 0) {
                    }
                    if (gps_error_cnt == 100) {
                        log_warning("Too many errors from GPSD; trying to reconnect!");
                        gps_close(run_state.gpsd);
                        run_state.reconnect_gpsd = true;
                        gps_error_cnt = 0;
                    }
                    continue;
                }

            }
        }
    */
}