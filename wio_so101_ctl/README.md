# Wio SO-101 Controller

This firmware is for embedded-side testing of a Wio Terminal handheld
controller for a SO-ARM100 / SO-101 robot arm.

The Wio Terminal provides:

- a small on-device arm visualization
- selected joint display
- fine/coarse relative joint movement commands
- emergency stop command
- rescan/sync command
- USB CDC serial protocol for host integration

## Files

- `wio_so101_ctl.ino` - main Wio Terminal firmware
- `arm_viz.h` - side-view arm visualization helper

## Build

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio_so101_ctl
```

## Upload

```bash
arduino-cli upload -p /dev/tty.usbmodemXXXX --fqbn Seeeduino:samd:seeed_wio_terminal wio_so101_ctl
```

Replace `/dev/tty.usbmodemXXXX` with the Wio Terminal port.

## Wio Controls

| Control | Action |
| --- | --- |
| 5-way left | Move selected joint negative |
| 5-way right | Move selected joint positive |
| 5-way press | Toggle fine/coarse step |
| Button A | Select next joint |
| Button B | Send emergency stop command |
| Button C | Send rescan/sync command |

Movement commands are only emitted when:

- the host link is online
- torque state is enabled

## Joint Order

| Servo ID | Display Name |
| --- | --- |
| 1 | `Rotation` |
| 2 | `Pitch` |
| 3 | `Elbow` |
| 4 | `Wrist_P` |
| 5 | `Wrist_R` |
| 6 | `Jaw` |

## Wio To Host Protocol

```text
! WIO READY
R
E
M <servo_id> <delta_degrees>
P
```

| Command | Meaning |
| --- | --- |
| `! WIO READY` | Wio boot message |
| `R` | Request rescan/sync and torque enable |
| `E` | Request emergency stop / torque disable |
| `M <servo_id> <delta_degrees>` | Request relative movement for one servo |
| `P` | Request fresh position/state data |

## Host To Wio Protocol

```text
P <tick1> <tick2> <tick3> <tick4> <tick5> <tick6>
O <online_count> <total_count>
T <0_or_1>
ST <status_text>
```

| Message | Meaning |
| --- | --- |
| `P ...` | Servo positions in ticks |
| `O ...` | Online servo count |
| `T 1` | Torque enabled |
| `T 0` | Torque disabled |
| `ST ...` | Status text displayed on Wio |

## Manual Serial Test

After flashing, open a serial monitor at `115200`.

Expected boot output:

```text
! WIO READY
```

Send these lines from the serial monitor to simulate an online arm:

```text
P 2048 2048 2048 2048 2048 2048
O 6 6
T 1
ST Embedded test ready
```
