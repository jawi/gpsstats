/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2019 jawi
 *   License: Apache License 2.0
 */

#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct config {
    uid_t priv_user;
    gid_t priv_group;

    char *gpsd_host;
    char *gpsd_port;
    char *gpsd_device;

    char *client_id;
    char *mqtt_host;
    uint16_t mqtt_port;
    uint8_t qos;
    bool retain;

    bool use_tls;
    bool use_auth;

    char *username;
    char *password;

    char *cacertpath;
    char *cacertfile;
    char *certfile;
    char *keyfile;
    char *tls_version;
    char *ciphers;
    bool verify_peer;
} config_t;

/**
 * Reads the configuration from the given file.
 *
 * @param file the configuration file to read, cannot be null.
 * @return the configuration object, or NULL in case of reading failures.
 */
config_t *read_config(const char *file);

/**
 * Free all resources taken up by the given configuration.
 *
 * @param config the configuration object to free, may be null.
 */
void free_config(config_t *config);

#endif /* CONFIG_H_ */