# Wio LeRobot Control

Wio Terminal firmware sketches for controlling LeRobot workflows and robot-side
test hubs.

## Sketches

- `wio_so101_ctl` - SO-101 / SO-ARM100 PC-hub controller. It shows a small arm
  visualization, selects joints, sends relative joint moves, and handles
  emergency stop / rescan commands over USB serial.
- `wio_b601_ctl` - B601 / arm-dm run console. It lists models and datasets from
  bambot + lerobot-easy, starts or stops local runs, starts or stops dataset
  replay, and displays compact run status.
- `wio_easy_ctl` - lerobot-easy Run page controller. It connects directly to
  the easy Web Serial button, sends only input events, and renders screen
  payloads owned by easy.

## Requirements

- Wio Terminal
- Arduino IDE or `arduino-cli`
- Seeed SAMD board package for Wio Terminal
- Wio Terminal LCD support from the Seeed board package

The board FQBN used by `arduino-cli` is:

```bash
Seeeduino:samd:seeed_wio_terminal
```

## Build

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio_so101_ctl
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio_b601_ctl
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio_easy_ctl
```

## Upload

Replace `/dev/tty.usbmodemXXXX` with the Wio Terminal serial port:

```bash
arduino-cli upload -p /dev/tty.usbmodemXXXX --fqbn Seeeduino:samd:seeed_wio_terminal wio_b601_ctl
```

Use `wio_so101_ctl` or `wio_easy_ctl` instead of `wio_b601_ctl` when flashing
those controllers.

## Serial Baud

All sketches use USB serial at `115200`.

## License

MIT
