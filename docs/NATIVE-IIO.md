# Native AD936x / Zynq / PlutoSDR reception

This fork adds a direct **libiio** receiver to IridiScope. It does not need
SoapySDR, an SDR++ server, GNU Radio or an IQ-file relay. The application runs
on the Windows PC; the SDR board runs its normal IIO/iiod firmware.

## Compatible hardware contract

- Zynq7010/Zynq7020 boards with AD9361, AD9363 or AD9364 and ADI-compatible IIO
  firmware, including HamGeek/Pluto-compatible boards and ADALM-Pluto.
- The context must expose `ad9361-phy`, input control `voltage0`, output
  `altvoltage0` **RX LO**, and input scan channels `voltage0`/`voltage1` on
  `cf-ad9361-lpc`, `cf-ad9361-A`, or another compatible RX core.
- RX1 scalar signed IQ in 16-bit containers, 1..16 valid bits. libiio handles
  byte order, sample-bit shifts and sign extension; each channel's scan pointer
  and buffer stride are respected. Other formats are rejected explicitly.
- The firmware and RF frontend must actually receive **1616..1626.5 MHz**.
  A board whose current RX LO minimum is 2.4 GHz cannot receive Iridium in that
  configuration. The application reports the rejected tune and does not modify
  the device tree, firmware identity, persistent environment or frequency limits.
- A Zynq processor alone is not an SDR. Unrelated IIO sensors, AD936x boards with
  different channel contracts, and non-AD936x radios are not claimed compatible.
  Existing native HackRF, Airspy, RTL-SDR and SDRplay sources remain separate.

Only RX is streamed. TX samples, TX LO and TX gain are never configured.
`altvoltage0` is an output *control* channel for the RX oscillator and is not
an enabled transmitter.

## Connect from the GUI

1. Power the radio and connect USB or Ethernet. Close other software using its
   receiver. For direct libusb, use the board/vendor's supported Windows USB
   driver; USB-network mode uses the board's existing network driver.
2. Open **Control → Input → Type → Zynq / Pluto / IIO**.
3. Enter `ip:192.168.2.1` for a board using that address. Use the real address
   if yours differs. Or enter the `usb:...` URI reported by libiio discovery.
   Clear the field and click **Discover / Refresh** for automatic discovery.
   Discovery filters out contexts without compatible AD936x RX channels.
4. Begin at **2.5 Msps**, RF bandwidth **0 (auto)**, port `A_BALANCED`, RX AGC.
   Start RX. Use higher rates only if the USB/network link and host can sustain
   them. At 2.5 Msps the waterfall covers only part of the 10.5 MHz Iridium band.
5. Stop RX before changing the URI, rate, bandwidth or RF port. Gain/AGC and
   frequency/PPM correction can be changed during reception. RF port names and
   gain limits depend on the board; firmware rejections appear in the UI.

There is no portable AD936x bias-tee switch. Use an external bias tee/supply
when an active antenna or LNA needs power.

The actual sample rate and LO are read back from the device. Stream errors
stop delivery and wake the engine instead of leaving it waiting forever.
Stop cancels a blocked libiio refill before joining the receive thread.

## Headless diagnostics (PowerShell)

```powershell
.\IridiScope.exe --iio-list
.\IridiScope.exe --iio-list ip:192.168.2.1
.\IridiScope.exe --iio-scan ip:192.168.2.1 10 2500000
```

The scan syntax is `--iio-scan URI [seconds] [rate_hz]`. A stream error is
printed and returns a nonzero exit code. No connected board is needed to open
IQ files or use the other built-in sources.

## Build on Windows x64

Use one matching MSYS2 prefix throughout. Current CI uses **UCRT64**; the
portable cross-build uses MINGW64 libraries. Do not mix their DLLs. Install
its toolchain, CMake, Ninja, pkgconf, libusb, libxml2 and zstd packages as listed
at the top of `scripts/build_iio.ps1`, plus MSYS2 `git`, `make` and `tar`.
From PowerShell at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1
powershell -ExecutionPolicy Bypass -File scripts\build_iio.ps1
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -RequireIio -IioRoot "$PWD\build-deps\iio"
Copy-Item .\build-deps\iio\bin\libiio.dll .\build\
```

This pins libiio **v0.26**, commit
`a0eca0d2bf10326506fb762f0eec14255b27bef5`, with USB and network backends.
New libiio 1.x headers are a different API; an unadapted 1.x library is not a
replacement. `-RequireIio` prevents accidentally building a release without
this backend. Keep `libiio.dll` and its dependent DLLs alongside the executable.
CMake copies the common MSYS2 runtime DLLs; custom prefixes require packaging
those DLLs from the prefix as well. The portable release includes them.

## Portable build contents

The x64 build includes native IIO, HackRF, Airspy and RTL-SDR support.
SDRplay requires its separately installed vendor API and was not included in
this portable build. Optional libacars ARINC-622 detail decoding was not built;
IridiScope's existing native message decoders remain available.

For UCRT64, install the corresponding `mingw-w64-ucrt-x86_64-*` packages and
pass `-Mingw C:\msys64\ucrt64` to all three PowerShell scripts. `bootstrap.ps1`
also accepts `-MsysBash` when MSYS2 is installed outside `C:\msys64`.

## Tests

```powershell
cmake -S tests -B build-tests -DLIBIIO_INCLUDE_DIR="$PWD\build-deps\iio\include"
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

The test uses the official libiio header with a deterministic fake device and
exercises the real receiver and live-source adapter: discovery, selected
indices, RX-only setup, padded/shifted IQ, normalization, rate readback, gain,
cancellation, failed initialization, disconnect and restart. These checks do
not prove over-the-air reception on physical hardware. No physical SDR is
attached to the build environment.
