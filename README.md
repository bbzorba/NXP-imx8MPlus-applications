# NXP i.MX 8M Plus applications

Applications for the NXP FRDM-IMX8MPLUS board (i.MX 8M Plus: 4× Cortex-A53
running Linux, plus a Cortex-M7 inside the same SoC). They are developed on
a Windows PC and cross-compiled, because the board's Yocto image gets
rebuilt and anything installed on it by hand is wiped.

| Folder | What it is |
|---|---|
| [home_hub_node](home_hub_node) | Sends the board's temperatures and system metrics to the ESP32-C6 Home Hub every 10 s ([architecture](home_hub_node/ARCHITECTURE.md)) |

## 1. Toolchain (Windows host, WSL)

The board runs glibc 2.43 on aarch64. Anything older works too, so Ubuntu's
cross-compiler in WSL is enough for applications:

```bash
wsl --install -d Ubuntu-22.04          # once, if WSL isn't set up
sudo apt-get install gcc-aarch64-linux-gnu make
```

For an exact match with the image, build the image's SDK instead
(`bitbake <image> -c populate_sdk`), install it in WSL, then source its
`environment-setup-armv8a-poky-linux` script before building and pass
`CC="$CC"` to make.

## 2. Build and deploy

From WSL, in an application folder:

```bash
make                  # cross-compile into build/
make deploy           # scp to the board, install and (re)start the systemd service
```

`BOARD` defaults to `root@192.168.178.171`; override with
`make deploy BOARD=root@<ip>`. The board's root user has no password, so
`ssh`/`scp` need no key setup.

## 3. Board notes

- **Model:** `NXP FRDM-IMX8MPLUS` (the hostname is `imx8mpevk`).
- **Sensors the kernel exposes:** two die temperatures (`cpu-thermal`,
  `soc-thermal`, trip point 85 °C) and the USB-C PD controller's
  voltage/current monitor, which only reads non-zero while the port supplies
  power. There are no environment sensors on the board.
- **Cortex-M7:** inside the same SoC, not a separate chip. The current
  device tree (`imx8mp-frdm.dtb`) only exposes the HiFi4 DSP to remoteproc.
  To use the M7 from Linux, boot `imx8mp-frdm-rpmsg.dtb` (already on the
  boot partition): in U-Boot, `setenv fdtfile imx8mp-frdm-rpmsg.dtb;
  saveenv`. Then load firmware from `/lib/firmware` with
  `echo <fw>.elf > /sys/class/remoteproc/remoteprocN/firmware` and
  `echo start > .../state`, and talk to it over RPMsg (`/dev/ttyRPMSG*`).
  Zephyr also supports the M7 directly (board `imx8mp_evk/mimx8ml8/m7`).
