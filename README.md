# Industrial Motor Condition-Monitoring & Predictive Maintenance System

Multi-sensor system on STM32 to detect early mechanical fault signatures in industrial motors — before failure, not after.

**Status: In active development.** Vibration sensing is working and validated on a live running motor. Current sensing is in progress. This README reflects the real state of the build at all times.

---

## Why this exists

Unplanned motor failure is one of the most expensive routine events in a factory — downtime, emergency repair, lost production. Large plants buy imported condition-monitoring systems priced far beyond what small and mid-size Indian factories can justify. This project explores how much fault-detection capability can be built at hobbyist-to-SME cost using a general-purpose MCU and low-cost sensors.

Motor faults announce themselves before failure — through vibration signatures (bearing wear, imbalance, misalignment), current signatures (load anomalies, winding issues), heat, and sound. The system's goal: catch those announcements.

## System architecture

```
                        ┌────────────────────────────┐
  ADXL345 (vibration) ──┤ I2C                        │
                        │                            │
  SCT-013 (current) ────┤ ADC ← analog front-end     │──► UART telemetry
                        │       (burden R + DC bias) │    (live data out)
  Thermal IR (planned) ─┤ I2C/ADC                    │
                        │      STM32F446RE           │
  Acoustic (planned) ───┤ ADC   (ARM Cortex-M4)      │
                        └────────────────────────────┘

  Processing roadmap: raw capture → RMS / threshold anomaly flags → ML fault classification
```

## Hardware

| Component | Role | Interface | Status |
|-----------|------|-----------|--------|
| STM32 Nucleo-F446RE | Main controller (Cortex-M4 @ 180 MHz) | — | Active |
| ADXL345 | 3-axis vibration sensing | I2C | ✅ Working, validated on live motor |
| SCT-013 | Non-invasive current sensing (CT clamp) | ADC via custom analog front-end | 🔧 In progress |
| Thermal IR sensor | Temperature signature | I2C/ADC | Planned |
| Acoustic sensor | Acoustic emission | ADC | Planned |

### Current-sensing front end (in progress)

The SCT-013 outputs an AC signal that the STM32 ADC cannot read directly. The conditioning network in development:

- **Burden resistor** to convert the CT's current output to a measurable voltage
- **DC-bias network** (resistor divider) to shift the AC waveform into the ADC's 0–3.3 V window
- Currently debugging noise coupling in the sensing path; next steps are calibration against a known load and RMS computation on-chip

## What's validated so far

- ADXL345 configured and read over I2C (register-level init: data format, rate, measurement mode)
- Live vibration data captured from a **running motor under real load** — not bench simulation
- Resolved I2C bus-level signal-integrity failures during bring-up (documented in [docs/debug-log.md](docs/debug-log.md))
- Streaming telemetry over UART for capture and analysis

## Roadmap

1. ✅ Vibration acquisition (ADXL345 / I2C) — done, validated
2. 🔧 Current sensing (SCT-013 + analog front-end) — conditioning network built, calibration next
3. RMS + threshold-based anomaly flags on vibration and current channels
4. Thermal + acoustic channels
5. Feature extraction (RMS, peak frequency) and off-device ML fault classification
6. Longer term: on-device inference

## Repo layout

```
├── Core/               STM32CubeIDE project source (main, drivers, config)
├── docs/
│   ├── debug-log.md    Problems hit and how they were resolved
│   └── wiring.md       Sensor wiring and front-end schematic notes
└── data/               Sample captured sensor data
```

## Tools

STM32CubeIDE · Embedded C (STM32 HAL, register-level configuration where needed) · Git

## Team

Built by a 4-member team — final-year Electronics Engineering, Bangalore Institute of Technology.

This repo is maintained by [Parshwa Sangame](https://github.com/YOUR-USERNAME) — my focus areas in the project: sensor integration and bring-up (ADXL345/I2C), the current-sensing analog front end, debugging, and component sourcing/BOM. Part of a broader interest in affordable industrial hardware for Indian SMEs — see my [sector research](https://github.com/YOUR-USERNAME/india-hardware-opportunities).
