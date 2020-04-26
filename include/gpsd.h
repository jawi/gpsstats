/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */

#ifndef _GPSD_H
#define _GPSD_H

#include <time.h>

#include "config.h"

/**
 * Defines the handle that is to be used to talk to the GPSD routines.
 */
typedef struct gpsd_handle gpsd_handle_t;

/**
 * Represents statistics about our connection to GPSD.
 */
typedef struct gpsd_stats {
    uint32_t events_recv;
    uint32_t events_send;
    time_t last_event;
} gpsd_stats_t;

/**
 * Allocates and initializes a new GPSD handle, but does not connect to GPSD yet, @see #connect_gpsd.
 *
 * @param config the configuration options.
 * @returns a new #gpsd_handle_t instance, or NULL in case no memory was available.
 */
gpsd_handle_t *gpsd_init(const config_t *config);

/**
 * Destroys and frees all previously allocated resources.
 *
 * @param handle the GPSD handle, cannot be NULL.
 */
void gpsd_destroy(gpsd_handle_t *handle);

/**
 * Connects to GPSD using a given configuration.
 *
 * @param handle the GPSD handle, cannot be NULL;
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int gpsd_connect(gpsd_handle_t *handle);

/**
 * Disconnects from a GPSD server.
 *
 * @param handle the GPSD handle, cannot be NULL.
 * @return 0 upon success, or a non-zero value in case of errors.
 */
int gpsd_disconnect(gpsd_handle_t *handle);

/**
 * Returns the file descriptor to the GPSD server.
 *
 * @param handle the GPSD handle, cannot be NULL.
 * @return a file descriptor, or -1 in case of errors.
 */
int gpsd_fd(gpsd_handle_t *handle);

/**
 * Reads data from GPSD, and, if present, returns it as event payload.
 *
 * NOTE: in case of a returned event payload, the caller of this method is
 * responsible for freeing the memory.
 *
 * @param handle the GPSD handle, cannot be NULL;
 * @param result the pointer to a char-buffer to put the event payload in.
 * @return 0 if no data was returned, a negative value in case of errors.
 */
int gpsd_read_data(gpsd_handle_t *handle, char **result);

/**
 * Dumps statistics about the GPSD connection at info logging level.
 * 
 * @param handle the GPSD handle, cannot be NULL.
 * @return the GPSD statistics.
 */
gpsd_stats_t gpsd_dump_stats(gpsd_handle_t *handle);

#endif
