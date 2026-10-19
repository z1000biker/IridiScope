# IridiScope — native IIO fork

Fork of [SarahRoseLives/IridiScope](https://github.com/SarahRoseLives/IridiScope),
with native **libiio RX support for Zynq7010/AD936x, PlutoSDR and compatible
IIO radios**. The upstream project and original authorship are retained.

**[Connection, hardware compatibility, Windows build and tests](docs/NATIVE-IIO.md)**

Select **Zynq / Pluto / IIO**, enter e.g. `ip:192.168.2.1`, begin at 2.5 Msps,
and start RX. USB URIs and filtered device discovery are also supported.
Physical reception on a real board remains to be verified.

A native Windows application for receiving, decoding and visualising **Iridium**
satellite signals. Load an IQ recording or plug in an SDR, and IridiScope shows a
live spectrum and waterfall, demodulates Iridium bursts, parses the messages,
decodes voice calls, reassembles ACARS/SBD traffic, and plots the satellites on
a world map.

---

## 📥 Upstream official Windows builds

**Download the official Windows builds from
<https://sarahsforge.dev/products/iridiscope>.**

Builds from this fork are independent builds and are not official upstream releases.
The upstream download does not contain the IIO additions described here.

---

## Features

- Live spectrum + scrolling waterfall with configurable FFT size and averaging.
- Full Iridium burst detection, DQPSK demodulation and native message parser
  (IRA, IBC, IDA, IMS, ITL, SY, IP, VO).
- Satellite tracking and ring-alert positions on an equirectangular world map.
- Voice call capture with in-app playback, plus ACARS/SBD message decoding.
- Sources: IQ file, **SDRplay**, **HackRF**, **Airspy**, **RTL-SDR**, and native
  **libiio AD936x**. Live VFO covers 1616–1626.5 MHz; captured bandwidth depends
  on the selected sample rate. Controls depend on the receiver.
- Automatic update notifications.

## Building

Requirements: Windows, [MSYS2](https://www.msys2.org/) with the `mingw-w64`
toolchain, CMake and Ninja, and PowerShell.

```powershell
# from the repository root
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1   # build the vendored FFTW
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

The executable is produced at `build\IridiScope.exe`. SDR support is detected
automatically at configure time; if the SDRplay API, libhackrf or libairspy are
not found, that source is simply disabled.

Handy headless modes: `--info <file>`, `--scan <file> [seconds]`,
`--sdr-list`, `--hrf-list`, `--ap-list`, `--rtl-list`, `--check-update`,
`--iio-list [URI]`, `--iio-scan URI [seconds] [rate_hz]`.

## Credits

IridiScope is built on the work of others:

- **iridium-sniffer** (muccc) — burst detection and demodulation core.
- **iridium-toolkit** (Sec, schneider) — message-format reference.
- **osmo-ir77** (Sylvain Munaut / Osmocom) — Iridium AMBE voice vocoder.
- **Dear ImGui** (Omar Cornut) and **ImPlot** (Evan Pezent).
- **GLFW**, **FFTW**, **miniaudio**, **stb**.
- SDR backends: **SDRplay API**, **libhackrf** (Great Scott Gadgets),
  **libairspy** (Airspy), **libiio** (Analog Devices).

SDR integration was modelled on **AirScope**. Map imagery courtesy of NASA.

## License

Released under the GNU General Public License v3.0.
