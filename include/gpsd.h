/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */

#ifndef _GPSD_H
#define _GPSD_H

#include "config.h"

/**
 * Defines the handle that is to be used to talk to the GPSD routines.
 */
typedef struct gpsd_handle gpsd_handle_t;

/**
 * Allocates and initializes a new GPSD handle, but does not connect to GPSD yet, @see #connect_gpsd.
 *
 * @param config the configuration options.
 * @returns a new #mqtt_handle_t instance, or NULL in case no memory was available.
 */
gpsd_handle_t *gpsd_init(const config_t *config);

/**
 * Destroys and frees all previously allocated resources.
 *
 * @param handle the GPSD handle.
 */
void gpsd_destroy(gpsd_handle_t *handle);

/**
 * Connects to GPSD using a given configuration.
 *
 * @param handle the GPSD handle;
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int gpsd_connect(gpsd_handle_t *handle);

/**
 * Disconnects from a GPSD server.
 *
 * @param handle the GPSD handle.
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int gpsd_disconnect(gpsd_handle_t *handle);

/**
 * Returns the file descriptor to the GPSD server.
 *
 * @param handle the GPSD handle.
 * @return a file descriptor, or -1 in case of errors.
 */
int gpsd_fd(gpsd_handle_t *handle);

char *gpsd_read_data(gpsd_handle_t *handle);

#endif
