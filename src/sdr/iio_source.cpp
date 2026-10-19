// Native receive-only libiio 0.x backend for AD9361/AD9363/AD9364 radios.
// SPDX-License-Identifier: GPL-3.0-only
#include "sdr/sdr_source.h"
#ifdef HAS_LIBIIO
#include <iio.h>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <utility>

namespace iridi {
namespace {
std::string iio_error(int code) {
    char text[256] = {};
    iio_strerror(code < 0 ? -code : code, text, sizeof(text));
    return text;
}
struct RxLayout {
    iio_device *phy = nullptr, *rx = nullptr;
    iio_channel *control = nullptr, *lo = nullptr, *i = nullptr, *q = nullptr;
};
bool find_layout(iio_context *ctx, RxLayout &d) {
    // The Linux AD9361 driver uses this name also for AD9363/AD9364.
    d.phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!d.phy) return false;
    d.control = iio_device_find_channel(d.phy, "voltage0", false);
    d.lo = iio_device_find_channel(d.phy, "altvoltage0", true); // RX LO, not TX
    const char *rx_names[] = {"cf-ad9361-lpc", "cf-ad9361-A"};
    for (const char *name : rx_names) {
        d.rx = iio_context_find_device(ctx, name);
        if (d.rx) break;
    }
    // Custom HDL may rename the RX core. Require a non-output scan IQ pair.
    if (!d.rx) {
        for (unsigned n = 0; n < iio_context_get_devices_count(ctx); ++n) {
            auto *dev = iio_context_get_device(ctx, n);
            auto *i = iio_device_find_channel(dev, "voltage0", false);
            auto *q = iio_device_find_channel(dev, "voltage1", false);
            if (i && q && iio_channel_is_scan_element(i) && iio_channel_is_scan_element(q)) {
                d.rx = dev;
                break;
            }
        }
    }
    if (!d.rx) return false;
    d.i = iio_device_find_channel(d.rx, "voltage0", false);
    d.q = iio_device_find_channel(d.rx, "voltage1", false);
    return d.control && d.lo && d.i && d.q &&
           iio_channel_is_scan_element(d.i) && iio_channel_is_scan_element(d.q);
}
bool supported_format(const iio_channel *ch) {
    const auto *f = iio_channel_get_data_format(ch);
    return f && f->length == 16 && f->bits > 0 && f->bits <= 16 &&
           f->bits + f->shift <= 16 && f->is_signed && f->repeat == 1;
}

class IioSource final : public SdrSource {
public:
    ~IioSource() override { stop(); }
    std::vector<SdrDeviceInfo> listDevices() override;
    void setUri(const std::string &uri) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ctx_ && uri_ != uri) { uri_ = uri; devices_.clear(); }
    }
    void setRfPort(const std::string &port) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ctx_) port_ = port;
    }
    void setBandwidth(double hz) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ctx_) bandwidth_ = hz;
    }
    void setCenterFreq(double hz) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!std::isfinite(hz) || hz <= 0 || hz > 6.1e9) {
            error_ = "Invalid IIO RX frequency"; return;
        }
        if (ctx_) {
            if (!write_ll(layout_.lo, "frequency", tuned(hz, ppm_))) return;
            long long actual = 0;
            if (!read_ll(layout_.lo, "frequency", actual)) return;
            center_ = actual / (1.0 + ppm_ / 1e6);
        } else center_ = hz;
    }
    void setSampleRate(double hz) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ctx_) { error_ = "Stop IIO RX before changing sample rate"; return; }
        rate_ = hz;
    }
    void setGain(double db) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!std::isfinite(db)) { error_ = "Invalid IIO RX gain"; return; }
        if (!ctx_ || apply_gain(db)) gain_ = db;
    }
    void setBiasTee(bool) override {} // No generic AD936x bias-tee attribute.
    void setPpm(double ppm) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!std::isfinite(ppm) || std::fabs(ppm) > 1000) {
            error_ = "IIO PPM correction must be within +/-1000"; return;
        }
        if (ctx_ && !write_ll(layout_.lo, "frequency", tuned(center_, ppm))) return;
        ppm_ = ppm;
    }
    double centerFreq() const override { std::lock_guard<std::mutex> lock(mutex_); return center_; }
    double sampleRate() const override { std::lock_guard<std::mutex> lock(mutex_); return rate_; }
    std::string lastError() const override { std::lock_guard<std::mutex> lock(mutex_); return error_; }
    bool running() const override { return running_.load(); }
    bool start(int index, SdrSampleCb cb, std::string &err) override;
    void stop() override;
private:
    static long long tuned(double hz, double ppm) { return std::llround(hz * (1.0 + ppm / 1e6)); }
    bool write_ll(iio_channel *ch, const char *attr, long long value) {
        int r = iio_channel_attr_write_longlong(ch, attr, value);
        if (r >= 0) return true;
        error_ = std::string("IIO RX ") + attr + "=" + std::to_string(value) + ": " + iio_error(r) +
                 ". Check firmware frequency/rate limits (Iridium requires 1616-1626.5 MHz).";
        return false;
    }
    bool read_ll(iio_channel *ch, const char *attr, long long &value) {
        int r = iio_channel_attr_read_longlong(ch, attr, &value);
        if (r >= 0) return true;
        error_ = std::string("IIO RX read ") + attr + ": " + iio_error(r); return false;
    }
    bool write_str(iio_channel *ch, const char *attr, const char *value) {
        auto r = iio_channel_attr_write(ch, attr, value);
        if (r >= 0) return true;
        error_ = std::string("IIO RX ") + attr + ": " + iio_error((int)r); return false;
    }
    bool apply_gain(double db) {
        if (!write_str(layout_.control, "gain_control_mode", db < 0 ? "slow_attack" : "manual")) return false;
        if (db < 0) return true;
        int r = iio_channel_attr_write_double(layout_.control, "hardwaregain", db);
        if (r >= 0) return true;
        error_ = "IIO RX hardwaregain: " + iio_error(r); return false;
    }
    void cleanup() {
        if (buffer_) { iio_buffer_destroy(buffer_); buffer_ = nullptr; }
        if (layout_.i) iio_channel_disable(layout_.i);
        if (layout_.q) iio_channel_disable(layout_.q);
        if (ctx_) { iio_context_destroy(ctx_); ctx_ = nullptr; }
        layout_ = {};
        callback_ = {};
    }
    void receive();
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    iio_context *ctx_ = nullptr;
    iio_buffer *buffer_ = nullptr;
    RxLayout layout_;
    SdrSampleCb callback_;
    std::string uri_, port_ = "A_BALANCED", error_;
    double center_ = 1621250000.0, rate_ = 2500000.0, bandwidth_ = 0, gain_ = -1, ppm_ = 0;
    std::vector<SdrDeviceInfo> devices_;
};

std::vector<SdrDeviceInfo> IioSource::listDevices() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SdrDeviceInfo> found;
    auto probe = [&](const std::string &uri, const std::string &description) {
        iio_context *ctx = iio_create_context_from_uri(uri.c_str());
        if (!ctx) return;
        const int timeout_result = iio_context_set_timeout(ctx, 1500);
        (void)timeout_result;
        RxLayout layout;
        if (find_layout(ctx, layout)) {
            found.push_back({(int)found.size(), description.empty() ? "AD936x / Zynq / PlutoSDR" : description, uri});
        }
        iio_context_destroy(ctx);
    };
    if (!uri_.empty()) probe(uri_, "AD936x (explicit URI)");
    else {
        auto *scan = iio_create_scan_context(nullptr, 0);
        if (scan) {
            iio_context_info **info = nullptr;
            auto count = iio_scan_context_get_info_list(scan, &info);
            for (ssize_t n = 0; n < count; ++n) {
                const char *uri = iio_context_info_get_uri(info[n]);
                const char *description = iio_context_info_get_description(info[n]);
                if (uri) probe(uri, description ? description : "");
            }
            if (info) iio_context_info_list_free(info);
            iio_scan_context_destroy(scan);
        }
    }
    devices_ = found;
    return found;
}

bool IioSource::start(int index, SdrSampleCb cb, std::string &err) {
    stop();
    // Preserve discovery order until explicit Refresh; no index substitution.
    bool discover = false;
    { std::lock_guard<std::mutex> lock(mutex_); discover = uri_.empty() && devices_.empty(); }
    if (discover) listDevices();
    std::lock_guard<std::mutex> lock(mutex_);
    error_.clear(); err.clear();
    auto fail = [&](const std::string &why) {
        error_ = why; err = why; cleanup(); return false;
    };
    if (!cb) return fail("IIO RX sample callback is empty");
    if (!std::isfinite(rate_) || rate_ < 2083334 || rate_ > 61440000 ||
        !std::isfinite(bandwidth_) || bandwidth_ < 0 || bandwidth_ > 56000000)
        return fail("IIO rate must be 2083334..61440000 Hz; bandwidth 0 (auto) or <=56000000 Hz");
    std::string uri = uri_;
    if (uri.empty()) {
        if (index < 0 || (size_t)index >= devices_.size())
            return fail("No compatible AD936x IIO device at selected index. Refresh or enter ip:/usb: URI.");
        uri = devices_[index].serial;
    }
    ctx_ = iio_create_context_from_uri(uri.c_str());
    if (!ctx_) return fail("Cannot open IIO URI " + uri + ": " + iio_error(errno));
    int r = iio_context_set_timeout(ctx_, 1500);
    if (r < 0) return fail("IIO context timeout: " + iio_error(r));
    if (!find_layout(ctx_, layout_))
        return fail("IIO device lacks ad9361-phy RX control/LO and a compatible voltage0/voltage1 scan pair");
    double bw = bandwidth_ > 0 ? bandwidth_ : std::fmin(rate_, 56000000.0);
    if (!write_str(layout_.control, "rf_port_select", port_.c_str()) ||
        !write_ll(layout_.control, "sampling_frequency", std::llround(rate_)) ||
        !write_ll(layout_.control, "rf_bandwidth", std::llround(bw)) ||
        !write_ll(layout_.lo, "frequency", tuned(center_, ppm_)) || !apply_gain(gain_))
        return fail(error_);
    long long actual_rate = 0, actual_lo = 0;
    if (!read_ll(layout_.control, "sampling_frequency", actual_rate) ||
        !read_ll(layout_.lo, "frequency", actual_lo)) return fail(error_);
    if (actual_rate < 2083334 || actual_rate > 61440000 || actual_lo <= 0)
        return fail("IIO device returned invalid sample rate or LO frequency");
    rate_ = (double)actual_rate;
    center_ = actual_lo / (1.0 + ppm_ / 1e6);
    // Select only RX1 I/Q. Never enable a TX device or alter firmware settings.
    for (unsigned n = 0; n < iio_device_get_channels_count(layout_.rx); ++n)
        iio_channel_disable(iio_device_get_channel(layout_.rx, n));
    if (!supported_format(layout_.i) || !supported_format(layout_.q))
        return fail("Unsupported IIO IQ format: need signed scalar 1..16-bit samples in 16-bit containers");
    iio_channel_enable(layout_.i); iio_channel_enable(layout_.q);
    buffer_ = iio_device_create_buffer(layout_.rx, 32768, false);
    if (!buffer_) return fail("IIO RX buffer: " + iio_error(errno));
    callback_ = std::move(cb);
    running_.store(true);
    try { thread_ = std::thread(&IioSource::receive, this); }
    catch (const std::exception &e) { running_.store(false); return fail(e.what()); }
    return true;
}

void IioSource::stop() {
    running_.store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buffer_) iio_buffer_cancel(buffer_);
    }
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    cleanup();
}

void IioSource::receive() {
    try {
        const float scale_i = std::ldexp(1.0f, 1 - (int)iio_channel_get_data_format(layout_.i)->bits);
        const float scale_q = std::ldexp(1.0f, 1 - (int)iio_channel_get_data_format(layout_.q)->bits);
        std::vector<float> samples(32768 * 2);
        while (running_.load()) {
            auto bytes = iio_buffer_refill(buffer_);
            if (!running_.load()) break;
            if (bytes < 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                error_ = "IIO RX stream: " + iio_error((int)bytes);
                break;
            }
            if (bytes == 0) continue;
            const auto step = iio_buffer_step(buffer_);
            const char *end = (const char *)iio_buffer_end(buffer_);
            const char *pi = (const char *)iio_buffer_first(buffer_, layout_.i);
            const char *pq = (const char *)iio_buffer_first(buffer_, layout_.q);
            if (step < 4 || !pi || !pq || !end) throw std::runtime_error("Invalid IIO RX buffer layout");
            int count = 0;
            // Separate channel pointers and libiio conversion handle padding,
            // byte order, sign extension and nonzero sample-bit shifts.
            const char *last = pi > pq ? pi : pq;
            if (last > end || end - last < 2) throw std::runtime_error("Truncated IIO RX buffer");
            const auto available = 1 + (end - last - 2) / step;
            for (; count < available && count < 32768; ++count) {
                int16_t i = 0, q = 0;
                iio_channel_convert(layout_.i, &i, pi + count * step);
                iio_channel_convert(layout_.q, &q, pq + count * step);
                samples[2 * count] = i * scale_i;
                samples[2 * count + 1] = q * scale_q;
            }
            if (count) callback_(samples.data(), count);
        }
    } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = std::string("IIO RX: ") + e.what();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = "IIO RX: unexpected stream failure";
    }
    running_.store(false);
}
} // namespace
std::unique_ptr<SdrSource> make_iio_source() { return std::make_unique<IioSource>(); }
} // namespace iridi
#endif
