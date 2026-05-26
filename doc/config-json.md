# Runtime JSON Config

`wave-home` now runs without `homebridge` at runtime.

The server reads two JSON files from `config/` by default:

- `config/appliances.json`: appliance inventory
- `config/server_state.json`: persisted active gesture set, bindings, and future server-owned settings

If you launch the packaged binary from `bin/`, the default config root is `../config`.
You can override it with `--config-root`.

## appliances.json

`appliances.json` is the authoritative device list.

Minimal shape:

```json
{
  "version": 1,
  "appliances": [
    {
      "id": "living-room-tv",
      "name": "Living Room TV",
      "kind": "tizen",
      "room": "Living Room",
      "includeDefaultInputs": true,
      "transport": {
        "kind": "tizen",
        "endpoint": "wss://192.168.0.10:8002",
        "options": {
          "ip": "192.168.0.10",
          "mac": "AA:BB:CC:DD:EE:FF",
          "token": "12345678",
          "securePort": 8002,
          "apiPort": 8001,
          "timeoutMs": 1500
        }
      },
      "inputs": [
        {
          "id": "netflix",
          "label": "Netflix",
          "kind": "app",
          "triggerMode": "pulse",
          "triggerCommands": [
            {
              "channel": "app",
              "payload": "11101200001"
            }
          ]
        }
      ]
    }
  ]
}
```

### Appliance rules

- `id` should be stable once bindings exist.
- `kind` currently supports `tizen` and `tuya`.
- `transport.endpoint` is required.
- For `tizen`, `includeDefaultInputs` defaults to `true`, which automatically adds:
  - `power`
  - `volume_up`
  - `volume_down`
  - `channel_up`
  - `channel_down`
  - `home`
- Custom `inputs` are optional and are merged before the default Tizen inputs.

### Input fields

- `id`: stable control id used by bindings
- `label` or `labelKey`: display text
- `kind`: `command`, `power`, `volume`, `channel`, `navigation`, `app`, `source`, or `toggle`
- `triggerMode`: `pulse` or `toggle`
- `triggerCommands`, `onCommands`, `offCommands`: arrays of `{ "channel", "payload" }`
- `meta`: optional extra metadata exposed through the device API

## server_state.json

`server_state.json` is written by the server and survives restarts.

Shape:

```json
{
  "version": 1,
  "activeSetId": "set0",
  "settings": {},
  "bindings": [
    {
      "deviceId": "living-room-tv",
      "controlId": "power",
      "controlLabel": "Power",
      "gestureClassId": 1,
      "triggerMode": "toggle",
      "repeatIntervalMs": 600
    }
  ]
}
```

### Persistence behavior

- Binding changes are saved immediately.
- Active gesture-set changes are saved immediately.
- On startup, invalid bindings are skipped instead of crashing the server.
- Bindings are dropped if:
  - the appliance id no longer exists
  - the control id no longer exists
  - the gesture class does not exist in the active gesture set
  - the file contains duplicate gesture assignments or duplicate control bindings

## Manual workflow

1. Stop the server if you plan to hand-edit `server_state.json`.
2. Edit `config/appliances.json` to add or remove appliances.
3. Start the server.
4. Use the dashboard to assign gestures.
5. Confirm that `config/server_state.json` now contains the active set and bindings.
