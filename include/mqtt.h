/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */

#ifndef _MQTT_H
#define _MQTT_H

#include "config.h"

/**
 * Defines the handle that is to be used to talk to the MQTT routines.
 */
typedef struct mqtt_handle mqtt_handle_t;

/**
 * Represents statistics about our connection to MQTT.
 */
typedef struct mqtt_stats {
    uint32_t events_send;
    time_t last_event;
} mqtt_stats_t;

/**
 * Allocates and initializes a new MQTT handle, but does not connect to MQTT yet, @see #connect_mqtt.
 *
 * @param config the configuration options.
 * @returns a new #mqtt_handle_t instance, or NULL in case no memory was available.
 */
mqtt_handle_t *mqtt_init(const config_t *config);

/**
 * Destroys and frees all previously allocated resources.
 *
 * @param handle the MQTT handle.
 */
void mqtt_destroy(mqtt_handle_t *handle);

/**
 * Connects to MQTT using a given configuration.
 *
 * @param handle the MQTT handle;
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int mqtt_connect(mqtt_handle_t *handle);

/**
 * Disconnects from a MQTT server.
 *
 * @param handle the MQTT handle.
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int mqtt_disconnect(mqtt_handle_t *handle);

/**
 * Performs the reading of MQTT data.
 *
 * @param handle the MQTT handle.
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int mqtt_read_data(mqtt_handle_t *handle);

bool mqtt_want_write(mqtt_handle_t *handle);

/**
 * Performs the writing of MQTT data.
 *
 * @param handle the MQTT handle.
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int mqtt_write_data(mqtt_handle_t *handle);

/**
 * Performs the various operations on MQTT data.
 *
 * @param handle the MQTT handle.
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int mqtt_misc_loop(mqtt_handle_t *handle);

/**
 * Returns the file descriptor to the MQTT server.
 *
 * @param handle the MQTT handle.
 * @return a file descriptor, or -1 in case of errors.
 */
int mqtt_fd(mqtt_handle_t *handle);

/**
 * Sends an event with data to the MQTT server.
 *
 * @param handle the MQTT handle;
 * @param event_data the event data to send, cannot be NULL.
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int mqtt_send_event(mqtt_handle_t *handle, const char *event_data);

/**
 * Dumps statistics about the MQTT connection at info logging level.
 * 
 * @param handle the MQTT handle, cannot be NULL.
 * @return the MQTT statistics.
 */
mqtt_stats_t mqtt_dump_stats(mqtt_handle_t *handle);

#endif