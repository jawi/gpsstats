# gpsstats

gpsstats is a small utility that connects to a (remote) GPSD instance and
extracts a couple of its statistics and writes it to a MQTT topic as a JSON
payload.

## Usage

```raw
gpsstats [-d] [-f] [-c config] [-p pidfile] [-u user[:group]] [-v]
```

where

```raw
-d  enables verbose logging
    (default: false);
-f  prevents gpsstats from running as daemon
    (default: false);
-c  provides the path to gpsstats configuration
    (default: /etc/gpsstats.cfg);
-p  provides the path to gpsstats pidfile
    (default: /run/gpsstats.pid);
-u  provides the user (and group) under which the daemon should be run
    (default: nobody / nogroup);
-v  prints out the version number of gpsstats and exits;
-h  prints out a short help text and exits.
```

### Signals

While running, gpsstats listens, aside the "normal" signals like `SIGTERM`
and `SIGKILL`, also to following signals:

- `SIGHUP`: when received will cause the main configuration file to be
  reloaded. Connections to both gpsd and mosquitto will be closed and
  reopened!
- `SIGUSR1`: when received will output some runtime statistics about
  gpsstats. This feature is mainly intended to see whether gpsstats is
  still receiving and transmitting data.

## Configuration

Gpsstats can be configured by a simple configuration file that is read upon
startup. By default, it expects the configuration file to reside in
`/etc/gpsstats.cfg`, but you can override it by means of the `-c` argument.

The configuration file is expected to be a plain YAML file encoded as UTF-8.
Note that not all YAML constructs are supported, only simple blocks.

The default configuration is defined as:

```yaml
# gpsstats configuration

gpsd:
   # The hostname or IP address of GPSD;
   # Defaults to localhost.
   host: localhost
   # The port of GPSD.
   # Defaults to 2947.
   port: 2947
   # If defined, GPSD should return information for this particular
   # device only. If omitted, all information of all devices will be
   # returned.
   # By default, all devices are used.
   device: /dev/gpsd0

mqtt:
   # Denotes how the MQTT client identifies itself to the MQTT broker.
   # Defaults to gpsstats.
   client_id: gpsstats_zeus
   # The hostname or IP address of the MQTT broker
   # Defaults to localhost.
   host: localhost
   # The port of the MQTT broker, use 8883 for TLS connections.
   # Defaults to 1883, or 8883 if TLS settings are defined.
   port: 1883
   # Denotes what quality of service to use:
   #   0 = at most once, 1 = at lease once, 2 = exactly once.
   # Defaults to 1.
   qos: 1
   # Whether or not the MQTT broker should retain messages for
   # future subscribers. Defaults to true.
   retain: true

   auth:
      # The username to authenticate against the MQTT broker. By default,
      # no authentication is used.
      username: foo
      # The password to authenticate against the MQTT broker. By default,
      # no authentication is used.
      password: bar

   tls:
      # The path to the CA certificates. Either this setting *or* the
      # 'ca_cert_file' setting should be given to enable TLS connections!
      # By default, no path is defined.
      ca_cert_path: /etc/ssl/certs
      # The CA certificate file, encoded in PEM format.
      # By default, no file is defined.
      ca_cert_file: /etc/ssl/certs/ca.pem
      # The client certificate file, encoded in PEM format.
      # By default, no file is defined.
      cert_file: gpsstats.crt
      # The client private key file, encoded in PEM format.
      # By default, no file is defined.
      key_file: gpsstats.key
      # Whether or not the identity of the MQTT broker should be verified.
      # use with case: only disable this setting when debugging TLS
      # connection problems! Defaults to true.
      verify_peer: yes
      # Denotes what TLS version should be used. Can be one of "tlsv1.0",
      # "tlsv1.1", "tlsv1.2" or "tlsv1.3".
      # Defaults to "tlsv1.2"
      tls_version: "tlsv1.2"
      # What TLS ciphers should be used for the TLS connection.
      # Defaults to an empty string, denoting that the default ciphers
      # of the SSL library should be used.
      ciphers: "TLSv1.2"

###EOF###
```

## Output

The event data is published on the MQTT topic `gpsstats` as a JSON object,
for example (output is formatted for readability):

```json
{
   "time":1587837604.000000000,
   "sats_used":12,
   "sats_visible":23,
   "tdop":0.830000,
   "avg_snr":28.833333,
   "toff":0.205150,
   "pps":-0.000001,
   "sats.gps":8,
   "sats.glonass":4
}
```

**Note**: the actual output of the JSON object is compressed to a single line
without newlines. The order of fields is not guaranteed to be the same across
restarts of gpsstats.

The fields in the JSON object have the following semantics:

| Field        | Description                                                                |
|--------------|----------------------------------------------------------------------------|
| time         | the time at which the event data from GPSD was received                    |
| sats_visible | the number of satellites that are currently visible by the GPS device      |
| sats_used    | the number of satellites that are used by the GPS device                   |
| sats.*name*  | the number of specific GPS/GLONASS/SBAS/... satellites used; NOTE:         |
|              | satellites that are not used are omitted!                                  |
| avg_snr      | the average SNR from all used satellites                                   |
| tdop         | the TDOP value as calculatd by GPSD                                        |
| toff         | the TOFF value as calculated by GPSD                                       |

## Development

### Compilation

gpsstats requires the following build dependencies:

- [libudaemon](https://github.com/jawi/libudaemon) (0.8 or later);
- [libgps](https://gitlab.com/gpsd/gpsd) (3.19 or later);
- [libmosquitto](https://mosquitto.org/) (1.5.5 or later);
- [libyaml](https://github.com/yaml/libyaml) (0.2 or later).

Gpsstats is developed to run under Linux, but can/may run on other operating
systems as well, YMMV.

Gpsstats is now CMake based. To compile gpsstats, do the following:

```sh
$ cd build
$ cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
...
-- Configuring done
-- Generating done
-- Build files have been written to: /path/to/gpsstats/build
$ make
...
```

All build artifacts, including the binaries, are placed in the `build`
directory.

### Finding memory leaks

You can use valgrind to test for memory leaks:

```sh
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose \
         ./build/gpsstats -f -d -c gpsstats.cfg
```

Let it run for a while and terminate the process with `CTRL+C`. The results
should indicate that all heap blocks were freed and no memory leaks are
possible.

## Installation

To install gpsstats, you should copy the `gpsstats` binary from the `build`
directory to its destination location, or run `make install` in the `build`
directory.

In addition, you should copy or create the configuration file in `/etc`
(or whatever location you want to use). By default, gpsstats assumes the
configuration file at `/etc/gpsstats.cfg`.

## License

gpsstats is licensed under Apache License 2.0.

## Author

gpsstats is written by Jan Willem Janssen `j dot w dot janssen at lxtreme dot nl`.

## Copyright

(C) Copyright 2020, Jan Willem Janssen.
