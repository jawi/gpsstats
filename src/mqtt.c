/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <mosquitto.h>
#include <udaemon/ud_logging.h>

#include "mqtt.h"

#define TOPIC "gpsstats"

#define MOSQ_ERROR(s) \
	((s) == MOSQ_ERR_ERRNO) ? strerror(errno) : mosquitto_strerror((s))

typedef enum conn_state {
    INITIALIZED,
    NOT_CONNECTED,
    CONNECTED,
    RECONNECTING,
    DISCONNECTED,
} conn_state_t;

struct mqtt_handle {
    struct mosquitto *mosq;
    conn_state_t conn_state;
    char *host;
    int port;
    bool retain;
    int qos;

    time_t next_reconnect_attempt;
    int next_delay_value;
};

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

static bool mqtt_needs_to_reconnect(int status) {
    return status == MOSQ_ERR_NO_CONN ||
           status == MOSQ_ERR_CONN_REFUSED ||
           status == MOSQ_ERR_CONN_LOST ||
           status == MOSQ_ERR_TLS ||
           status == MOSQ_ERR_AUTH ||
           status == MOSQ_ERR_UNKNOWN;
}

mqtt_handle_t *mqtt_init(const config_t *cfg) {
    mosquitto_lib_init();

    mqtt_handle_t *handle = malloc(sizeof(mqtt_handle_t));
    if (!handle) {
        log_error("failed to create MQTT handle: out of memory!");
        return NULL;
    }
    bzero(handle, sizeof(mqtt_handle_t));

    struct mosquitto *mosq = mosquitto_new(cfg->client_id, true /* clean session */, handle);
    if (!mosq) {
        log_error("failed to create new mosquitto instance");
        goto err_cleanup;
    }

    handle->mosq = mosq;
    handle->host = cfg->mqtt_host;
    handle->port = cfg->mqtt_port;
    handle->retain = cfg->retain;
    handle->qos = cfg->qos;
    handle->conn_state = INITIALIZED;
    handle->next_reconnect_attempt = -1L;
    handle->next_delay_value = 1;

    int status;

    if (cfg->use_tls) {
        log_debug("setting up TLS parameters on mosquitto instance");

        status = mosquitto_tls_insecure_set(handle->mosq, false);
        if (status != MOSQ_ERR_SUCCESS) {
            log_error("failed to disable insecure TLS: %s", MOSQ_ERROR(status));
            goto err_cleanup;
        }

        status = mosquitto_tls_opts_set(handle->mosq,
                                        cfg->verify_peer, cfg->tls_version, cfg->ciphers);
        if (status != MOSQ_ERR_SUCCESS) {
            log_error("failed to set TLS options: %s", MOSQ_ERROR(status));
            goto err_cleanup;
        }

        status = mosquitto_tls_set(handle->mosq,
                                   cfg->cacertfile, cfg->cacertpath, cfg->certfile, cfg->keyfile, NULL);
        if (status != MOSQ_ERR_SUCCESS) {
            log_error("failed to set TLS settings: %s", MOSQ_ERROR(status));
            goto err_cleanup;
        }
    }

    if (cfg->use_auth) {
        log_debug("setting up authentication on mosquitto instance");

        status = mosquitto_username_pw_set(handle->mosq, cfg->username, cfg->password);
        if (status != MOSQ_ERR_SUCCESS) {
            log_error("failed to set authentication credentials: %s", MOSQ_ERROR(status));
            goto err_cleanup;
        }
    }

    mosquitto_connect_callback_set(handle->mosq, my_connect_cb);
    mosquitto_disconnect_callback_set(handle->mosq, my_disconnect_cb);
    mosquitto_log_callback_set(handle->mosq, my_log_callback);

    return handle;

err_cleanup:
    if (handle->mosq) {
        mosquitto_destroy(handle->mosq);
    }
    free(handle);

    mosquitto_lib_cleanup();

    return NULL;
}

void mqtt_destroy(mqtt_handle_t *handle) {
    if (handle) {
        mosquitto_destroy(handle->mosq);
        handle->mosq = NULL;

        free(handle);
    }

    mosquitto_lib_cleanup();
}

int mqtt_connect(mqtt_handle_t *handle) {
    int status = mosquitto_connect(handle->mosq, handle->host, handle->port, 60 /* keepalive */);
    if (status != MOSQ_ERR_SUCCESS) {
        log_warning("failed to connect to MQTT broker: %s", MOSQ_ERROR(status));
        return -1;
    }
    return 0;
}

int mqtt_disconnect(mqtt_handle_t *handle) {
    int status = mosquitto_disconnect(handle->mosq);
    if (status != MOSQ_ERR_SUCCESS && status != MOSQ_ERR_NO_CONN) {
        log_warning("failed to disconnect from MQTT broker: %s", MOSQ_ERROR(status));
        return -1;
    }
    return 0;
}

int mqtt_read_data(mqtt_handle_t *handle) {
    int status = mosquitto_loop_read(handle->mosq, 1 /* max_packets */);
    if (status != MOSQ_ERR_SUCCESS) {
        log_warning("Failed to read MQTT messages. Reason: %s", MOSQ_ERROR(status));
        return -1;
    }
    return 0;
}

int mqtt_write_data(mqtt_handle_t *handle) {
    int status;

    if (!mosquitto_want_write(handle->mosq)) {
        // Nothing to do...
        return 0;
    }

    status = mosquitto_loop_write(handle->mosq, 1 /* max_packets */);
    if (status != MOSQ_ERR_SUCCESS) {
        log_warning("Failed to write MQTT messages. Reason: %s", MOSQ_ERROR(status));
        return -1;
    }
    return 0;
}

int mqtt_misc_loop(mqtt_handle_t *handle) {
    int status = mosquitto_loop_misc(handle->mosq);
    if (status != MOSQ_ERR_SUCCESS) {
        log_warning("Failed to run misc loop of MQTT: %s", MOSQ_ERROR(status));
        return -1;
    }
    return 0;
}

int mqtt_fd(mqtt_handle_t *handle) {
    int fd = mosquitto_socket(handle->mosq);
    if (fd < 0) {
        log_error("Failed to obtain MQTT file descriptor!");
    }
    return fd;
}

void mqtt_send_event(mqtt_handle_t *handle, const char *event_data) {
    log_debug("Publishing event %s", event_data);

    int status = mosquitto_publish(handle->mosq, NULL /* message id */,
                                   TOPIC,
                                   (int) strlen(event_data), event_data,
                                   handle->qos,
                                   handle->retain);
    if (status) {
        log_warning("Failed to publish data to MQTT broker. Reason: %s", MOSQ_ERROR(status));
    }
}

// EOF