# Wio B601 Controller

Wio Terminal firmware for the bambot Web Serial model and dataset console. The
Wio talks newline-delimited JSON over USB serial to the browser bridge, and
bambot forwards commands and status to local lerobot-easy.

## Build

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio_b601_ctl
```

## Upload

```bash
arduino-cli upload -p /dev/tty.usbmodemXXXX --fqbn Seeeduino:samd:seeed_wio_terminal wio_b601_ctl
```

Replace `/dev/tty.usbmodemXXXX` with the Wio Terminal port.

## Controls

Use `C` to switch modes.

Model mode:

- `A`: stop run
- `B`: start local run
- `C`: switch to replay mode
- joystick left/up: previous model
- joystick right/down: next model

Replay mode:

- `A`: stop active replay
- `B`: replay selected dataset episode `0`
- `C`: switch to model mode
- joystick left/up: previous dataset
- joystick right/down: next dataset

## Wio To Browser

```json
{"type":"hello","seq":1}
{"type":"models","seq":2}
{"type":"prev_model","seq":3}
{"type":"next_model","seq":4}
{"type":"run_start","seq":5,"mode":"local"}
{"type":"run_stop","seq":6}
{"type":"status","seq":7}
{"type":"datasets","seq":8}
{"type":"prev_dataset","seq":9}
{"type":"next_dataset","seq":10}
{"type":"replay_start","seq":11,"episodeIndex":0}
{"type":"replay_stop","seq":12}
{"type":"set_mode","seq":13,"mode":"replay"}
```

## Browser To Wio

The firmware consumes compact messages from bambot, including:

```json
{"type":"ready","text":"Ready"}
{"type":"models","items":[{"id":"m1","name":"Pick Cube"}],"selected":0}
{"type":"selected","index":0,"name":"Pick Cube"}
{"type":"run","status":"Running","stage":"robot_client"}
{"type":"datasets","items":[{"id":"ds1","name":"Pick Dataset","episodes":3}],"selected":0}
{"type":"dataset_selected","index":0,"name":"Pick Dataset","episodes":3}
{"type":"replay","status":"Completed","dataset":"ds1","episode":0}
{"type":"error","code":"replay_failed","text":"Replay Failed"}
```
