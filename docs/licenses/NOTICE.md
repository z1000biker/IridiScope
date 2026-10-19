# Source and third-party notices

This is an independent fork of SarahRoseLives/IridiScope. Upstream's README
states GPLv3; the root LICENSE reproduces that license. Original copyright
notices and credits are retained in the source tree. The new IIO backend is
GPL-3.0-only.

The linked osmo-ir77 voice codec retains its original AGPL-3.0-or-later notices;
its AGPLv3 license text is included here. Other bundled components retain their
own licenses: see their source directories and the portable package's licenses
folder. The combined application must also respect the codec's AGPL terms.

Native IIO uses Analog Devices libiio v0.26 (LGPL-2.1-or-later). libusb,
libxml2, libiconv, zlib, xz and zstd have separate license notices in the portable
package. FFTW is statically linked from the vendored 3.3.10 source archive.
Dear ImGui, ImPlot, GLFW, stb and miniaudio remain vendored upstream components.
The existing native SDR integrations use libairspy, libhackrf and librtlsdr.

Application source: https://github.com/z1000biker/IridiScope
Upstream: https://github.com/SarahRoseLives/IridiScope
libiio: https://github.com/analogdevicesinc/libiio/tree/v0.26
Airspy host library: https://github.com/airspy/airspyone_host
HackRF: https://github.com/greatscottgadgets/hackrf
RTL-SDR: https://github.com/osmocom/rtl-sdr
MSYS2 binary dependencies and packaging recipes:
https://github.com/msys2/MINGW-packages
