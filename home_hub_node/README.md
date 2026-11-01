# home_hub_node – developer manual

Client node for the ESP32-C6 Home Hub
([ESP32C6-Zephyr-IoT-Projects](https://github.com/bbzorba/ESP32C6-Zephyr-IoT-Projects),
`applications/web_app`). Runs on the i.MX 8M Plus under Linux and POSTs
the board's readings to `http://home-hub/api/node` every 10 s.

| Document | Content |
|---|---|
| this README | build, deploy, configure, test |
| [ARCHITECTURE.md](ARCHITECTURE.md) | where it runs, main loop, data sources |

## 1. Build and deploy

From WSL, in this folder (toolchain: see the [top-level README](../README.md)):

```bash
make                                    # cross-compile into build/
make deploy                             # copy to /opt/home-hub/, install + restart the service
make deploy BOARD=root@<ip>             # other board address
```

After flashing a new Yocto image, run `make deploy` again (the image
wipes `/opt/home-hub`).

## 2. Configuration

Environment variables, set in `home-hub-node.service`:

| Variable | Default | Meaning |
|---|---|---|
| `HUB_HOST` | `home-hub` (resolved by the router) | hub name or IP |
| `HUB_PORT` | `80` | hub HTTP port |

## 3. Testing

| Test | How | Expected |
|---|---|---|
| Running | `ssh root@<board> systemctl status home-hub-node` | `active (running)` |
| Reports arrive | open the hub's dashboard | an `imx8mpevk` card, values change every 10 s |
| Hub down | unplug the hub; `journalctl -u home-hub-node -f` | `Hub not reachable, stopping until it is back` |
| Hub back | plug the hub in again | `Hub reachable again, resuming` within 60 s |
| Autostart | `reboot` the board without logging in | card back online after boot |

## 4. Report format

```json
{"id":"imx8mpevk","model":"NXP FRDM-IMX8MPLUS",
 "sensors":[{"name":"CPU temperature","value":46.00,"unit":"°C"}]}
```

At most 16 sensors, names ≤ 23 bytes, units ≤ 7 bytes (the hub's limits);
raw UTF-8, no `\uXXXX` escapes.
