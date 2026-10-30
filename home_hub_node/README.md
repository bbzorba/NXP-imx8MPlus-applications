# home_hub_node

Client node for the ESP32-C6 Home Hub
([ESP32C6-Zephyr-IoT-Projects](https://github.com/bbzorba/ESP32C6-Zephyr-IoT-Projects),
`applications/web_app`). Runs on the i.MX 8M Plus under Linux and, every
10 s, POSTs the board's readings to `http://home-hub/api/node`. The hub
shows them as a card on its dashboard and marks the node offline after
30 s without a report.

## What it sends

| Name | Source |
|---|---|
| CPU temperature, SoC temperature | `/sys/class/thermal/thermal_zone*` |
| `<hwmon> V`, `<hwmon> A` | `/sys/class/hwmon/*/in0_input`, `curr1_input` (only when non-zero) |
| CPU load | `/proc/stat`, since the previous report |
| CPU frequency | average of `scaling_cur_freq` over all cores |
| Load average (1 min), Memory used, Disk used (/), Uptime | `/proc`, `statvfs` |

Payload (the hub keeps up to 16 sensors per node, names up to 23 bytes):

```json
{"id":"imx8mpevk","model":"NXP FRDM-IMX8MPLUS",
 "sensors":[{"name":"CPU temperature","value":46.00,"unit":"°C"}]}
```

## Hub unavailable

Each report is one TCP connection. If it fails, the node stops sending and
only tries a TCP connect every 60 s; when that works it resumes. Nothing is
buffered while the hub is away.

## Build, deploy, autostart

```bash
make            # cross-compile (see the top-level README for the toolchain)
make deploy     # copies build/home_hub_node to /opt/home-hub/ and installs the service
```

It starts at boot through `home-hub-node.service` (systemd), not `.bashrc`:
`.bashrc` only runs when someone logs in to a shell, so after a reboot
without a login nothing would be sent. Logs: `journalctl -u home-hub-node`.

`HUB_HOST` (default `home-hub`, resolved by the router) and `HUB_PORT`
(default 80) can be set in the service file.

Because a rebuilt Yocto image wipes `/opt/home-hub`, run `make deploy`
again after flashing a new image, or add a recipe to your layer that
installs the binary and the service.
