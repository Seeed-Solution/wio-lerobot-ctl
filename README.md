# Wio SO-101 Embedded Test Firmware

This firmware is for embedded-side testing of a Wio Terminal handheld controller for a SO-ARM100 / SO-101 robot arm.

The Wio Terminal provides:

- a small on-device arm visualization
- selected joint display
- fine/coarse relative joint movement commands
- emergency stop command
- rescan/sync command
- USB CDC serial protocol for test host integration

This README only documents the Wio firmware behavior and its serial test protocol.

## Files

- `wio_so101_pc_hub.ino` - main Wio Terminal firmware
- `arm_viz.h` - side-view arm visualization helper

## Hardware Required

- Wio Terminal
- USB cable for flashing and serial testing
- SO-ARM100 / SO-101 arm test setup

## Software Required

- Arduino IDE or `arduino-cli`
- Seeed SAMD board package for Wio Terminal
- Wio Terminal LCD support from the Seeed board package

## Flash The Firmware

Open this sketch in Arduino IDE:

```text
wio_so101_pc_hub.ino
```

Select:

- board: `Seeeduino Wio Terminal`
- port: the Wio Terminal USB port

Then compile and upload.

With `arduino-cli`, the board FQBN is usually:

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal .
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn Seeeduino:samd:seeed_wio_terminal .
```

Replace `/dev/cu.usbmodemXXXX` with the Wio Terminal port.

## Firmware Behavior

On boot, the firmware:

1. starts USB Serial at `115200`
2. initializes the Wio Terminal LCD
3. draws the SO-101 side-view arm visualization
4. initializes all joints to the home tick value `2048`
5. sends this boot line over USB Serial:

```text
! WIO READY
```

The firmware expects a serial test host to send position, torque, and status lines back to Wio. If no host line is received for `3000 ms`, Wio shows the host link as offline.

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

The firmware uses six servo IDs:

| Servo ID | Display Name |
| --- | --- |
| 1 | `Rotation` |
| 2 | `Pitch` |
| 3 | `Elbow` |
| 4 | `Wrist_P` |
| 5 | `Wrist_R` |
| 6 | `Jaw` |

## Step Sizes

The current movement step sizes are:

- fine: `3.0 deg`
- coarse: `12.0 deg`

Press the 5-way joystick to switch between fine and coarse mode.

When the joystick is held left or right, the firmware emits repeated relative move commands at the firmware update interval.

## Screen UI

The Wio screen shows:

- side-view arm visualization
- selected joint
- selected joint position in degrees when available
- torque state: `READY` or `E-STOP`
- step mode: fine or coarse
- host state: `PC ok` or `PC --`
- status text from the test host

The visualization is a lightweight 2D approximation from `arm_viz.h`. It is intended for embedded test feedback, not precise robot kinematics.

## USB Serial Settings

| Setting | Value |
| --- | --- |
| baud | `115200` |
| framing | newline-terminated text line |
| line ending | `\n` |
| encoding | ASCII-compatible text |

The firmware ignores carriage returns.

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

Examples:

```text
M 1 +3.00
M 3 -3.00
E
R
```

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

Example host response:

```text
P 2048 2048 2048 2048 2048 2048
O 6 6
T 1
ST Online 6/6 servos
```

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

The Wio should show:

- `READY`
- selected joint information
- `PC ok`
- `Embedded test ready`

Now hold the 5-way joystick left or right. The serial monitor should receive movement lines such as:

```text
M 1 -3.00
M 1 +3.00
```

Press:

- `A` to switch selected joint
- 5-way press to switch step size
- `B` to emit `E`
- `C` to emit `R`

## Safety Notes

- Keep the arm clear before enabling torque.
- Treat `B` as the emergency stop button.
- Do not hold the joystick if the arm is near a hard stop.
- Start with fine mode when testing a new setup.
- Make sure servo IDs match `1..6` before moving the arm.

## Troubleshooting

### No Boot Line In Serial Monitor

Check:

- correct serial port
- baud is `115200`
- firmware upload completed
- Wio Terminal has reset after upload

### Wio Shows `PC --`

The firmware has not received a host line recently.

Send a status or position line such as:

```text
ST Embedded test ready
```

### Joystick Does Not Emit Move Commands

Movement output is gated by host link and torque state.

Send:

```text
P 2048 2048 2048 2048 2048 2048
T 1
ST Embedded test ready
```

Then hold 5-way left or right.

### Selected Joint Shows Offline

The latest `P ...` line may not include all six tick values.

Send:

```text
P 2048 2048 2048 2048 2048 2048
```

### Screen Does Not Update

Check:

- Wio Terminal board package is installed
- LCD library support is available from the board package
- the firmware was built for `Seeeduino Wio Terminal`

