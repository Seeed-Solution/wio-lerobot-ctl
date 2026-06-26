# Wio Easy Controller

Wio Terminal firmware for the lerobot-easy Run page Wio button.

This sketch keeps business logic in lerobot-easy:

- Wio sends compact input events over USB serial.
- lerobot-easy owns modes, model selection, dataset replay, jog routing, and
  robot serial ownership.
- Wio renders the latest `screen` payload from lerobot-easy.

## Build

```bash
arduino-cli compile --fqbn Seeeduino:samd:seeed_wio_terminal wio_easy_ctl
```

## Upload

```bash
arduino-cli upload -p /dev/tty.usbmodemXXXX --fqbn Seeeduino:samd:seeed_wio_terminal wio_easy_ctl
```

## Wio To easy

```json
{"type":"hello","seq":1}
{"type":"status","seq":2}
{"type":"input","key":"a","event":"press","seq":3}
{"type":"input","key":"b","event":"press","seq":4}
{"type":"input","key":"c","event":"press","seq":5}
{"type":"input","key":"left","event":"repeat","seq":6}
{"type":"input","key":"right","event":"repeat","seq":7}
{"type":"input","key":"up","event":"press","seq":8}
{"type":"input","key":"down","event":"press","seq":9}
```

## easy To Wio

```json
{
  "type": "screen",
  "mode": "model",
  "title": "MODEL",
  "primary": "Pick Red Block",
  "index": "1/4",
  "status": "Ready",
  "footer": "A Mode  B Start  C Stop",
  "connected": true
}
```

The firmware also accepts:

```json
{"type":"ready","text":"Ready"}
{"type":"error","text":"Bad Command"}
```

## Controls

- `A`: switch easy mode
- `B`: primary action for the current easy mode
- `C`: stop action for the current easy mode
- joystick left/right/up/down: selection or jog input, interpreted by easy
