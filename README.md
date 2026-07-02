Danfoss Eco
=============

The ``danfoss_eco`` climate platform creates a climate device which can be used to control a Danfoss Eco eTRV.

This component supports the following functionality:

- Switch between Manual and Schedule modes
- Set the target room temperature
- Show the current temperature
- Show current action (Heating/Idle)
- Show remaining battery level
- Managing multiple eTRVs from a single ESP32
- Reporting eTRV error status (with error codes)

This platform uses the ESP32 BLE peripheral on ESP32, which means, ``ble_client`` configuration should be provided.

Onboarding your device
------------------------
You should know the MAC address of your Danfoss Eco in order to complete the configuration. This information can be obtained in two ways:
1. Look it up in Danfoss Eco mobile app: `Settings->System Information->MAC Address`
2. Use `danfoss_eco_scanner` sensor and check the EspHome logs. Sample configuration for the scanner:
```yaml
esphome:
  name: etrv2wifi-scanner

esp32:
  board: esp32dev
  framework:
    type: esp-idf

logger:
  level: INFO

external_components:
  - source: github://borstel4711/esphome-danfoss-eco

sensor:
  - platform: danfoss_eco_scanner
    id: scanner
```
When the scanner is running, press the hardware button on your Danfoss Eco in order to speed up the discovery. Sample scanner output will look like this:
```
[01:40:19][I][danfoss_eco_scanner:027]: Found Danfoss eTRV, MAC: 00:04:2F:xx:yy:zz, Name: 0;0:04:2F:xx:yy:zz;eTRV
```

Once the MAC Adress is known, esphome component can be configured as follows:
```yaml
external_components:
  - source: github://borstel4711/esphome-danfoss-eco

esp32:
  board: esp32dev
  framework:
    type: esp-idf

ble_client:
  - mac_address: 00:04:2f:xx:yy:zz
    id: room_eco
climate:
  - platform: danfoss_eco
    name: "My Room eTRV"
    ble_client_id: room_eco
    battery_level:
      name: "My Room eTRV Battery Level"
    temperature:
      name: "My Room eTRV Temperature"
    update_interval: 30min
```

### Obtaining the `secret_key`
Danfoss Eco is using encrypted communication, which relies on the `secret_key`. This key can be obtained only if the hardware button was pressed before Bluetooth connection is established. Keep track of the EspHome logs to know, when the button should be pressed:
```
[01:25:01][I][logger:214]: Log initialized
[01:25:01][I][app:029]: Running through setup()...
[01:25:02][I][danfoss_eco:215]: [My Room eTRV] Short press Danfoss Eco hardware button NOW in order to allow reading the secret key
```
If the button was not pressed in time, failure to retrieve the `secret_key` will be logged. Restart the ESP32 and try again.
```
[01:25:13][W][danfoss_eco:148]: [My Room eTRV] Danfoss Eco hardware button was not pressed, unable to read the secret key
```

When component succeeds to read the `secret_key`, it will store the value in ESP32 flash, but it's recommended to explicitly add it to esphome component configuration.
```
[01:21:27][I][danfoss_eco:164]: [My Room eTRV] Consider adding below line to your danfoss_eco config:
[01:21:27][I][danfoss_eco:165]: [My Room eTRV] secret_key: deadbeefcafebabedeadbeefcafebabe 
[01:21:27][I][danfoss_eco:180]: [My Room eTRV] secret_key was saved to flash
```

Configuration options
------------------------

- **id** (*Optional*): Manually specify the ID used for code generation.
- **name** (**Required**, string): The name of the climate device.
- **ble_client_id** (**Required**): The ID of the BLE Client.
- **pin_code** (**Optional**, string): Device PIN code (if configured). Should be 4 characters numeric string.
- **secret_key** (**Required**, string): Device encryption key, 16 characters.
- **battery_level** (**Optional**, string): Remaining battery level sensor name. Sensor will not be created, if the name is not provided.
- **temperature** (**Optional**, string): Current temperature (Celsius) sensor name. Sensor will not be created, if the name is not provided.
- **update_interval** (*Optional*, default `15min`): How often the eTRV state is polled over BLE. Ignored when the device is managed by `danfoss_eco_manager`. See [Choosing an update_interval](#choosing-an-update_interval).

`danfoss_eco_manager` options:

- **devices** (**Required**, list): IDs of the `danfoss_eco` climate devices to manage.
- **update_interval** (*Optional*, default `15min`): How often a full polling cycle (all devices, one after the other) is started.
- **session_timeout** (*Optional*, default `60s`): Watchdog budget for a single device session. A session that runs longer is aborted so the cycle can continue with the next device.

> **NOTE:** Find more configuration examples in the repository root folder.


Managing multiple eTRVs (up to 5+ per ESP32)
--------------------------------------------

The recommended way to run several eTRVs on a single ESP32 is the `danfoss_eco_manager` component. It polls all devices **sequentially** — only one BLE connection is active at a time — which is what makes 5 devices per ESP32 stable:

```yaml
danfoss_eco_manager:
  update_interval: 15min   # one full cycle (all devices) per interval
  session_timeout: 60s     # watchdog: abort a hanging session, continue with the next device
  devices:
    - id: trv_room
    - id: trv_kitchen
    - id: trv_bedroom
    - id: trv_bathroom
    - id: trv_office
```

The manager takes over the polling of all listed devices (their own `update_interval` is disabled), aborts sessions that hang (`session_timeout`) so a single unreachable eTRV cannot stall the cycle, and flushes Home Assistant commands with priority as soon as the current BLE session finishes — so `set_temperature` etc. do not wait for the next polling cycle.

Each `ble_client` statically claims one BLE connection slot, so tell ESPHome how many you need (ESPHome derives the matching ESP-IDF `sdkconfig` options from this — no `sdkconfig.defaults` file is needed):

```yaml
esp32_ble:
  max_connections: 5   # one per ble_client entry (default is 3, maximum 9)
```

Since all devices are connected by MAC address, continuous BLE scanning is unnecessary and only competes with connections for radio time:

```yaml
esp32_ble_tracker:
  scan_parameters:
    continuous: false
```

See `example_4_manager.yaml` for a complete 5-device configuration.

Choosing an `update_interval`
-----------------------------

Polling is a trade-off between data freshness and eTRV battery life — every poll opens a BLE connection on the thermostat. The eTRV regulates the room temperature autonomously; polling only synchronizes its state with Home Assistant, and **Home Assistant commands are always sent immediately**, independent of the interval.

| update_interval | BLE sessions per device/day | Recommendation |
|-----------------|-----------------------------|----------------|
| 5min            | ~288                        | only for debugging |
| 15min (default) | ~96                         | good freshness/battery compromise |
| 30min           | ~48                         | battery-friendly, still fresh enough for room temperature |
| 60min           | ~24                         | maximum battery life |

> **NOTE:** Find more configuration examples in the repository root folder.


Most stable setup: wired PoE gateway
------------------------------------

For a rock-solid, always-on gateway (especially with 4–5 eTRVs) a **PoE Ethernet** board beats WiFi: dropping WiFi removes the WiFi/BLE 2.4 GHz radio coexistence contention — the biggest source of flaky BLE on ESP32 — and frees the RAM the WiFi stack would use. A board with an **external antenna** additionally improves range to eTRVs behind metal radiators.

`example_5_poe.yaml` is a complete config for the **Olimex ESP32-POE-ISO-WROVER-EA** (isolated PoE, 8 MB PSRAM, external antenna). Two board specifics worth noting:

- Uses `ethernet:` instead of `wifi:`. On the **WROVER** variant GPIO16/17 are taken by the PSRAM, so Olimex moved the Ethernet clock from GPIO17 to **GPIO0** — hence `clk: {mode: CLK_OUT, pin: GPIO0}` (on the non-WROVER board it stays GPIO17). ESPHome will warn that GPIO0/GPIO12 are strapping pins; that is expected for this hardware.
- Add an empty `psram:` block to enable the 8 MB PSRAM (extra heap headroom). Combined with Ethernet this leaves plenty of RAM, so `esp32_ble: max_connections: 5` can match the client count without a warning.

See Also
--------

This component is based on the work of other authors:
* [AdamStrojek libetrv](https://github.com/AdamStrojek/libetrv) (with additional features from [spin83](https://github.com/spin83/libetrv) fork)
* MQTT bridge for Danfoss eTRV by [keton](https://github.com/keton/etrv2mqtt) and Home Assistant add-on by [HBDK](https://github.com/HBDK/Eco2-Tools)

Other implementations:
* [Source code](https://github.com/dsltip/Danfoss-BLE) for controlling Danfoss ECO via cc2541 as a gateway from the desktop PC.
