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
    eh_id_t mqtt_event_handler_id;

    uint32_t gpsd_disconnects;
    uint32_t gpsd_connects;

    uint32_t mqtt_disconnects;
    uint32_t mqtt_connects;

} run_state_t;

static run_state_t _run_state = {
    .gpsd_event_handler_id = UD_INVALID_ID,
    .mqtt_event_handler_id = UD_INVALID_ID,
};

static void gpsstats_gps_callback(ud_state_t *ud_state, struct pollfd *pollfd, void *context);
static void gpsstats_mqtt_callback(ud_state_t *ud_state, struct pollfd *pollfd, void *context);

// task that disconnects from GPSD and reconnects to it...
static int gpsstats_reconnect_gpsd(ud_state_t *ud_state, uint16_t interval, void *context) {
    run_state_t *run_state = context;

    if (run_state->gpsd) {
        log_debug("Closing connection to GPSD...");

        gpsd_disconnect(run_state->gpsd);
        gpsd_destroy(run_state->gpsd);
        run_state->gpsd = NULL;

        // Update stats...
        run_state->gpsd_disconnects++;
    }

    if (ud_valid_event_handler_id(run_state->gpsd_event_handler_id)) {
        if (ud_remove_event_handler(ud_state, run_state->gpsd_event_handler_id)) {
            log_warning("Unable to remove GPSD event handler!");
        }
        run_state->gpsd_event_handler_id = UD_INVALID_ID;
    }

    run_state->gpsd = gpsd_init(run_state->config);
    if (run_state->gpsd == NULL) {
        log_warning("Unable to reinitialize GPSD! Out of memory?");
        return -ENOMEM;
    }

    if (gpsd_connect(run_state->gpsd)) {
        log_warning("Unable to connect to GPSD! Scheduling retry...");
        return interval * 2;
    }

    int fd = gpsd_fd(run_state->gpsd);
    if (fd) {
        if (ud_add_event_handler(ud_state, fd, POLLIN,
                                 gpsstats_gps_callback,
                                 run_state,
                                 &run_state->gpsd_event_handler_id)) {
            log_warning("Unable to add GPSD event handler!");
            return -EINVAL;
        }
    }

    // Update stats...
    run_state->gpsd_connects++;

    return 0;
}

// Called when data of gpsd is received...
static void gpsstats_gps_callback(ud_state_t *ud_state, struct pollfd *pollfd, void *context) {
    run_state_t *run_state = context;

    int status;
    bool need_reconnect = false;

    if (pollfd->revents & POLLIN) {
        char *event = { 0 };

        status = gpsd_read_data(run_state->gpsd, &event);
        if (status < 0) {
            need_reconnect = (status == -ENOTCONN);
        } else if (status > 0) {
            mqtt_send_event(run_state->mqtt, event);
            free(event);
        }
    }

    if (need_reconnect) {
        // ensure we no longer get any results from poll() while we're reconnecting...
        pollfd->events = 0;

        if (ud_schedule_task(ud_state, 1, gpsstats_reconnect_gpsd, run_state)) {
            log_warning("Failed to register (re)connect task for GPSD?!");
        }
    }
}

// task that disconnects from MQTT and reconnects to it...
static int gpsstats_reconnect_mqtt(ud_state_t *ud_state, uint16_t interval, void *context) {
    run_state_t *run_state = context;

    if (run_state->mqtt) {
        log_debug("Closing connection to MQTT...");

        mqtt_disconnect(run_state->mqtt);
        mqtt_destroy(run_state->mqtt);
        run_state->mqtt = NULL;

        // Update stats...
        run_state->mqtt_disconnects++;
    }

    if (ud_valid_event_handler_id(run_state->mqtt_event_handler_id)) {
        if (ud_remove_event_handler(ud_state, run_state->mqtt_event_handler_id)) {
            log_warning("Unable to remove MQTT event handler!");
        }
        run_state->mqtt_event_handler_id = UD_INVALID_ID;
    }

    run_state->mqtt = mqtt_init(run_state->config);
    if (run_state->mqtt == NULL) {
        log_warning("Unable to reinitialize MQTT! Out of memory?");
        return -ENOMEM;
    }

    if (mqtt_connect(run_state->mqtt)) {
        log_warning("Unable to connect to MQTT! Scheduling retry...");
        return interval * 2;
    }

    int fd = mqtt_fd(run_state->mqtt);
    if (fd) {
        if (ud_add_event_handler(ud_state, fd, POLLIN,
                                 gpsstats_mqtt_callback,
                                 run_state,
                                 &run_state->mqtt_event_handler_id)) {
            log_warning("Unable to add MQTT event handler!");
            return -EINVAL;
        }
    }

    // Update stats...
    run_state->mqtt_connects++;

    return 0;
}

// Called when data of mosquitto is received/to be transmitted...
static void gpsstats_mqtt_callback(ud_state_t *ud_state, struct pollfd *pollfd, void *context) {
    run_state_t *run_state = context;

    if (mqtt_want_write(run_state->mqtt)) {
        log_debug("Requesting to write MQTT data...");

        pollfd->events |= POLLOUT;
    } else if (pollfd->events & POLLOUT) {
        log_debug("Clearing MQTT data request...");

        pollfd->events = pollfd->events & ~POLLOUT;
    }

    int status;
    bool need_reconnect = false;

    if (pollfd->revents & POLLOUT) {
        // We can write safely...
        status = mqtt_write_data(run_state->mqtt);
        need_reconnect |= (status == -ENOTCONN);
    }
    if (pollfd->revents & POLLIN) {
        // We can read safely...
        status = mqtt_read_data(run_state->mqtt);
        need_reconnect |= (status == -ENOTCONN);
    }

    if (need_reconnect) {
        // ensure we no longer get any results from poll() while we're reconnecting...
        pollfd->events = 0;

        if (ud_schedule_task(ud_state, 1, gpsstats_reconnect_mqtt, run_state->config)) {
            log_warning("Failed to register (re)connect task for MQTT?!");
        }
    }
}

// Initializes GPSStats
static int gpsstats_init(ud_state_t *ud_state) {
    // Dump the configuration when running in debug mode...
    dump_config(ud_get_app_config(ud_state));

    // Connect to both services...
    if (ud_schedule_task(ud_state, 1, gpsstats_reconnect_mqtt, &_run_state)) {
        log_warning("Failed to register connect task for MQTT?!");
    }

    if (ud_schedule_task(ud_state, 1, gpsstats_reconnect_gpsd, &_run_state)) {
        log_warning("Failed to register connect task for GPSD?!");
    }

    return 0;
}

static void gpsstats_dump_stats(ud_state_t *ud_state, run_state_t *run_state) {
    (void)ud_state;

    gpsd_stats_t gpsd_stats = gpsd_dump_stats(run_state->gpsd);
    mqtt_stats_t mqtt_stats = mqtt_dump_stats(run_state->mqtt);

    log_info(PROGNAME " statistics:");

    log_info("GPSD connects: %d, disconnects: %d, events rx: %d, tx: %d, last seen: %d",
             run_state->gpsd_connects, run_state->gpsd_disconnects,
             gpsd_stats.events_recv, gpsd_stats.events_send,
             gpsd_stats.last_event);

    log_info("MQTT connects: %d, disconnects: %d, events tx: %d, last: %d",
             run_state->mqtt_connects, run_state->mqtt_disconnects,
             mqtt_stats.events_send,
             mqtt_stats.last_event);
}

static void gpsstats_signal_handler(ud_state_t *ud_state, ud_signal_t signal) {
    if (signal == SIG_HUP) {
        // Dump the configuration when running in debug mode...
        dump_config(ud_get_app_config(ud_state));

        // reconnect to both GPSD & MQTT...
        gpsstats_reconnect_gpsd(ud_state, 0, &_run_state);
        gpsstats_reconnect_mqtt(ud_state, 0, &_run_state);
    } else if (signal == SIG_USR1) {
        gpsstats_dump_stats(ud_state, &_run_state);
    }
}

static int gpsstats_mqtt_misc_loop(ud_state_t *ud_state, uint16_t interval, void *context) {
    (void)ud_state;
    run_state_t *run_state = context;

    mqtt_misc_loop(run_state->mqtt);
    return interval;
}

// Cleans up all resources...
static int gpsstats_cleanup(ud_state_t *ud_state) {
    (void)ud_state;

    log_debug("Closing connection to GPSD...");
    gpsd_disconnect(_run_state.gpsd);
    gpsd_destroy(_run_state.gpsd);

    log_debug("Closing connection to MQTT...");
    mqtt_disconnect(_run_state.mqtt);
    mqtt_destroy(_run_state.mqtt);

    free_config(_run_state.config);

    return 0;
}

int main(int argc, char *argv[]) {
    ud_config_t daemon_config = {
        .progname = PROGNAME,
        .initialize = gpsstats_init,
        .signal_handler = gpsstats_signal_handler,
        .cleanup = gpsstats_cleanup,
        // configuration handling...
        .config_parser = read_config,
        .config_cleanup = free_config,
    };

    // parse arguments...
    int opt;
    while ((opt = getopt(argc, argv, "c:dfhp:v")) != -1) {
        switch (opt) {
        case 'c':
            _run_state.conf_file = strdup(optarg);
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

    // Use defaults if not set explicitly...
    if (_run_state.conf_file == NULL) {
        _run_state.conf_file = strdup(CONF_FILE);
    }
    if (daemon_config.pid_file == NULL) {
        daemon_config.pid_file = strdup(PID_FILE);
    }

    ud_state_t *daemon = ud_init(&daemon_config);
    // Perform periodic tasks...
    ud_schedule_task(daemon, 5, gpsstats_mqtt_misc_loop, &_run_state);

    int retval = ud_main_loop(daemon);

    ud_destroy(daemon);

    free(_run_state.conf_file);
    free(daemon_config.pid_file);

    return retval;
}