<div align="center">

<img src="https://capsule-render.vercel.app/api?type=rect&color=0:8B0000,50:C1121F,100:FFB300&height=120&section=header&text=Predictive%20Maintenance%20Edge%20Node&fontSize=32&fontColor=FFFFFF&fontAlignY=55" />

**A four-sensor STM32 system that catches motor faults before they cause failure.**

<img src="https://img.shields.io/badge/Platform-STM32F446RE-00D4FF?style=flat-square" />
<img src="https://img.shields.io/badge/Firmware-Bare--Metal%20C-C1121F?style=flat-square" />
<img src="https://img.shields.io/badge/Sensors-4%2F4%20Online-00C853?style=flat-square" />
<img src="https://img.shields.io/badge/License-MIT-FFB300?style=flat-square" />

</div>

---

## What This Is

An edge device that watches an industrial motor and warns before it fails. Four sensors, current, vibration, acoustic, and temperature, feed a single STM32F446RE across ADC, SPI, and 1-Wire. Every cycle, each sensor is reduced to one feature, and together those four features describe the machine's health. The features stream live over UART to a PC-side dashboard, and the system can learn what "normal" looks like for a specific motor and set its own alarm thresholds from that baseline.

All firmware is written bare-metal, direct register access, no HAL, for full control over timing and low overhead.

---

## Why Bare-Metal

Using HAL gets a project running fast, but it hides the hardware. This project is written at register level on purpose: to understand exactly what the peripherals are doing, to keep timing predictable for the non-blocking sensor scheduling, and to keep overhead low on a system that streams continuously.

---

## System Architecture

```
                ┌─────────────────────────────┐
   SCT-013 ───► │  ADC (2 ch)                  │
   MAX4466 ───► │  Current + Acoustic          │
                │                              │
   ADXL345 ───► │  SPI (Mode 3)                │
                │  Vibration                   │
                │                              │──► Feature Vector
   DS18B20 ───► │  1-Wire (bit-banged, TIM2)   │    [I_rms, V_rms,
                │  Temperature                 │     A_rms, Temp]
                └─────────────────────────────┘
                         │
                         ▼  UART (USART2 → USB, 115200 baud)
                ┌─────────────────────────────┐
                │  bridge.py                   │
                │  Serial → JSON → HTTP        │
                └─────────────────────────────┘
                         │
                         ▼
                ┌─────────────────────────────┐
                │  pdm_dashboard_live.html     │
                │  Gauges · Scope · Radar plot │
                │  Normal / Warning / Critical │
                └─────────────────────────────┘
```

A developing fault typically shows up as more than one feature drifting at once, a motor under stress draws more current, runs hotter, and gets noisier and more vibratory. That correlated drift is what the system is built to catch.

---

## Sensor Details

| # | Feature | Sensor | Bus | Notes |
|---|---|---|---|---|
| 1 | Current (RMS) | SCT-013 non-invasive CT | ADC | 1.65V active DC-bias front end centers the AC signal in the 0–3.3V ADC range. True RMS via sum-of-squares with per-window DC-offset removal, calibrated to amps. |
| 2 | Vibration (RMS) | ADXL345 3-axis accelerometer | SPI (Mode 3) | Verifies the DEVID register reads 0xE5 at bring-up. RMS taken of acceleration magnitude about its mean, so gravity and mounting orientation cancel out. |
| 3 | Acoustic (RMS) | MAX4466 electret mic amp | ADC | Same sum-of-squares RMS path as current, reused on a second ADC channel. Least trusted signal, most prone to ambient noise. |
| 4 | Temperature | DS18B20 probe | 1-Wire, bit-banged (TIM2, microsecond timing) | Requires a mandatory pull-up on the data line. Chosen over an I2C IR sensor for a simpler, more reliable single-wire link. |

**Why sum-of-squares, not peak detection:** an earlier version used peak-to-peak min/max for the current channel. It produced phantom readings whenever there was electrical noise on the line, a single spike would register as a load change that never happened. Switching to true RMS by sum-of-squares fixed it, since a single noise spike barely moves an RMS average across a full window.

---

## Intelligence Layer

**Manual mode:** enter the motor's rated horsepower, and alarm thresholds are set automatically from NEC full-load-current tables.

**Learn mode:** run the system on a healthy motor for up to 10 minutes. It calculates the mean and standard deviation of each feature and sets thresholds statistically, mean + 3σ for a warning, mean + 5σ for a trip.

**Alarm priority:** vibration > temperature > current > acoustic. Acoustic is ranked last because it is the noisiest, least reliable signal of the four.

> **A note on "Learn mode":** this is adaptive **statistical** anomaly detection, mean plus k-sigma against a measured baseline. It is not a trained machine learning model, and it is described accurately as statistics rather than ML on purpose. It works well for this problem and does not need to be more complicated than it is.

---

## Software Stack

**Firmware (`four_sensor_uart_full.c`):** bare-metal STM32 firmware. Configures ADC (multi-channel scanning), SPI1, and a bit-banged 1-Wire driver on TIM2. Computes RMS per channel, applies Manual/Learn mode thresholding, and streams the feature vector over USART2.

**Bridge (`bridge.py`):** reads the UART stream on the PC side and serves it as live JSON over a small HTTP server, so the dashboard can be opened from any device on the network, including a phone over local WiFi.

**Dashboard (`pdm_dashboard_live.html`):** an industrial-style HMI. Real-time gauges for each feature, a feature-history scope, a live radar/signature plot, and an escalating Normal → Warning → Critical status display.

---

## Engineering Notes

- **FPU enabled** for floating-point RMS math. Skipping this causes a silent HardFault on floating-point instructions, one of the harder failure modes to trace on bare metal, since nothing in the code looks wrong.
- **Non-blocking scheduling:** the DS18B20's slow conversion time does not stall the fast analog sensors. Each sensor is serviced on its own timing, not in a single blocking loop.
- **Star-grounding:** analog and digital grounds are kept separate and joined at one point, to stop digital switching noise from coupling into the analog current and acoustic readings.
- **C-runtime startup:** an early build used a hand-rolled startup file that broke debugger variable visibility. Switching to CubeIDE's Empty Project startup fixed it.
- **I2C bus debugging:** `i2c_scanner.c` is kept in the repo as a standalone tool from earlier bus-level fault diagnosis work.

---

## Wiring

Full connection tables and circuit diagrams are in the repo:

- `PdM_Connections.pdf`, pin-by-pin wiring reference
- `pdm_circuit_diagram.png` / `.svg` / `.pdf`, schematic views

---

## Repository Structure

```
├── four_sensor_uart_full.c     # Main firmware (all 4 sensors, bare-metal)
├── bridge.py                   # Serial-to-browser bridge (Python)
├── pdm_dashboard_live.html     # Live HMI dashboard
├── adxl_test.c                 # Standalone vibration sensor test
├── mic_test.c                  # Standalone acoustic sensor test
├── ds18b20_test.c              # Standalone temperature sensor test
├── i2c_scanner.c               # I2C bus diagnostic tool
├── PdM_Connections.pdf         # Wiring reference
└── pdm_circuit_diagram.*       # Circuit diagrams (png/svg/pdf)
```

---

## Getting Started

1. Flash `four_sensor_uart_full.c` to an STM32 Nucleo-F446RE via STM32CubeIDE.
2. Wire the sensors per `PdM_Connections.pdf`.
3. Connect the board over USB and run `python bridge.py`.
4. Open the dashboard in a browser, or on a phone over local WiFi, at the address `bridge.py` prints on startup.

---

## Honest Scope and Limitations

- Current and temperature thresholds are grounded in real standards (NEC full-load-current tables). Vibration and acoustic thresholds are placeholders, meant to be set properly using Learn mode on an actual motor, not assumed defaults.
- The system is currently assembled on a breadboard. Perfboard or a soldered build is the recommended next step for deployment stability and a lower noise floor.
- Validation has been on the bench with live signal sources, not yet on a deployed industrial motor in the field.
- "Learn mode" is statistical baseline detection, not a trained machine learning model. See the Intelligence Layer section above.

---

## Roadmap

- [ ] Baseline vibration and acoustic thresholds on a real motor
- [ ] Move from breadboard to perfboard / custom PCB
- [ ] High-rate acquisition with DMA for true vibration/acoustic spectra
- [ ] Long-run field validation on a deployed motor

---

## Author

**Parshwa Sangame**
Final-year Electronics and Communication Engineering, Bangalore
[LinkedIn](https://in.linkedin.com/in/parshwa-sangame-a89484314) · sangame.parshwa@gmail.com

<img src="https://capsule-render.vercel.app/api?type=rect&color=0:FFB300,50:C1121F,100:8B0000&height=60&section=footer" />
