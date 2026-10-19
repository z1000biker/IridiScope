// Exercises the real backend and LiveIqSource against a deterministic IIO device.
// Uses the official libiio header; the fake device implements that ABI below.
#include <iio.h>
#include "sdr/live_iq_source.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <cerrno>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
struct iio_context {};
struct iio_device { bool phy; } phy{true}, rx{false};
struct iio_channel { int id; bool scan; } control{0,false}, lo{1,false}, ci{2,true}, cq{3,true};
struct iio_buffer {
    std::mutex mutex;
    std::condition_variable cv;
    bool cancelled = false, sent = false;
    unsigned char data[16] = {0, 0x80, 0xf0, 0x7f, 0,0,0,0, 0xf0,0x3f, 0,0xc0, 0,0,0,0};
};
struct iio_scan_context {};
struct iio_context_info {} info;
static iio_context_info *infos[] = {&info, nullptr};
static iio_data_format format{16,12,4,true,false,false,false,1,1};
static bool wrong_layout=false, fail_frequency=false, fail_buffer=false, fail_stream=false;
static long long requested_lo=0, actual_rate=2500001;
static std::string gain_mode;
static std::atomic<int> contexts{0}, buffers{0}, tx_writes{0};
extern "C" {
void iio_strerror(int err,char *s,size_t n) { std::snprintf(s,n,"error %d",err); }
iio_context *iio_create_context_from_uri(const char *uri) { if(std::strcmp(uri,"ip:missing")==0){ errno=ENODEV;return nullptr;} ++contexts; return new iio_context; }
void iio_context_destroy(iio_context *p) { --contexts; delete p; }
int iio_context_set_timeout(iio_context *,unsigned) { return 0; }
iio_device *iio_context_find_device(const iio_context *,const char *name) {
 if(wrong_layout)return nullptr;
 if(std::strcmp(name,"ad9361-phy")==0)return &phy;
 if(std::strcmp(name,"cf-ad9361-lpc")==0)return &rx;
 return nullptr;
}
unsigned iio_context_get_devices_count(const iio_context *) { return wrong_layout?0:2; }
iio_device *iio_context_get_device(const iio_context *,unsigned n) { return n?&rx:&phy; }
iio_channel *iio_device_find_channel(const iio_device *d,const char *name,bool output) {
 if(d->phy) {
  if(output && std::strcmp(name,"altvoltage0")==0)return &lo;
  if(!output && std::strcmp(name,"voltage0")==0)return &control;
 } else if(!output) {
  if(std::strcmp(name,"voltage0")==0)return &ci;
  if(std::strcmp(name,"voltage1")==0)return &cq;
 }
 return nullptr;
}
bool iio_channel_is_scan_element(const iio_channel *c){return c->scan;}
unsigned iio_device_get_channels_count(const iio_device *){return 2;}
iio_channel *iio_device_get_channel(const iio_device *,unsigned n){return n?&cq:&ci;}
void iio_channel_enable(iio_channel *){}
void iio_channel_disable(iio_channel *){}
const iio_data_format *iio_channel_get_data_format(const iio_channel *){return &format;}
ssize_t iio_channel_attr_write(const iio_channel *c,const char *attr,const char *value){
 if(c!=&control)++tx_writes;
 if(std::strcmp(attr,"gain_control_mode")==0)gain_mode=value;
 return (ssize_t)std::strlen(value);
}
int iio_channel_attr_write_longlong(const iio_channel *c,const char *attr,long long value){
 if(std::strcmp(attr,"frequency")==0){CHECK(c==&lo); if(fail_frequency)return -EINVAL; requested_lo=value;}
 return 0;
}
int iio_channel_attr_read_longlong(const iio_channel *,const char *attr,long long *value){
 *value=std::strcmp(attr,"sampling_frequency")==0?actual_rate:requested_lo;return 0;
}
int iio_channel_attr_write_double(const iio_channel *c,const char *,double){CHECK(c==&control);return 0;}
iio_buffer *iio_device_create_buffer(const iio_device *d,size_t,bool cyclic){CHECK(d==&rx);CHECK(!cyclic);if(fail_buffer){errno=ENOMEM;return nullptr;}++buffers;return new iio_buffer;}
void iio_buffer_destroy(iio_buffer *b){--buffers;delete b;}
ssize_t iio_buffer_refill(iio_buffer *b){
 std::unique_lock<std::mutex> lock(b->mutex);
 if(fail_stream)return -EIO;
 if(!b->sent){b->sent=true;return 16;}
 b->cv.wait(lock,[&]{return b->cancelled;});return -ECANCELED;
}
void iio_buffer_cancel(iio_buffer *b){std::lock_guard<std::mutex> lock(b->mutex);b->cancelled=true;b->cv.notify_all();}
ptrdiff_t iio_buffer_step(const iio_buffer *){return 8;} // padded scan stride
void *iio_buffer_first(const iio_buffer *b,const iio_channel *c){return (void*)(b->data+(c==&cq?2:0));}
void *iio_buffer_end(const iio_buffer *b){return (void*)(b->data+16);}
void iio_channel_convert(const iio_channel *,void *dst,const void *src){
 auto *p=(const unsigned char *)src;
 unsigned word=format.is_be ? (p[0]<<8)|p[1] : p[0]|(p[1]<<8);
 word >>= format.shift;
 unsigned mask=(1u<<format.bits)-1;
 word &= mask;
 int16_t value=(int16_t)((word&(1u<<(format.bits-1))) ? (word|~mask):word);
 std::memcpy(dst,&value,2);
}
iio_scan_context *iio_create_scan_context(const char *,unsigned){return new iio_scan_context;}
void iio_scan_context_destroy(iio_scan_context *s){delete s;}
ssize_t iio_scan_context_get_info_list(iio_scan_context *,iio_context_info ***p){*p=infos;return 1;}
void iio_context_info_list_free(iio_context_info **){}
const char *iio_context_info_get_uri(const iio_context_info *){return "usb:1.2.3";}
const char *iio_context_info_get_description(const iio_context_info *){return "Test Zynq7010 AD9363";}
}
static void reset(){wrong_layout=fail_frequency=fail_buffer=fail_stream=false;format={16,12,4,true,false,false,false,1,1};}
static void assert_clean(){CHECK(contexts==0);CHECK(buffers==0);CHECK(tx_writes==0);}
int main(){try {
 std::string err;
 auto s=iridi::make_iio_source();
 auto devs=s->listDevices();CHECK(devs.size()==1);CHECK(devs[0].serial=="usb:1.2.3");assert_clean();
 CHECK(!s->start(9,[](const float*,int){},err));CHECK(err.find("index")!=std::string::npos);assert_clean();
 s->setUri("ip:192.168.2.1");s->setCenterFreq(1621250000);s->setSampleRate(2500000);s->setPpm(10);
 std::mutex mutex;std::condition_variable cv;std::vector<float> got;
 CHECK(s->start(0,[&](const float *p,int n){std::lock_guard<std::mutex> lock(mutex);got.assign(p,p+n*2);cv.notify_all();},err));
 {std::unique_lock<std::mutex> lock(mutex);CHECK(cv.wait_for(lock,std::chrono::seconds(2),[&]{return !got.empty();}));}
 CHECK(got.size()==4);CHECK(got[0]==-1);CHECK(std::fabs(got[1]-2047.0f/2048)<1e-6);
 CHECK(std::fabs(got[2]-1023.0f/2048)<1e-6);CHECK(got[3]==-0.5f);
 CHECK(s->sampleRate()==actual_rate);CHECK(requested_lo==1621266213);CHECK(gain_mode=="slow_attack");
 s->setGain(35);CHECK(gain_mode=="manual");s->setGain(-1);CHECK(gain_mode=="slow_attack");
 s->setSampleRate(4000000);CHECK(s->sampleRate()==actual_rate);CHECK(!s->lastError().empty());
 auto t=std::chrono::steady_clock::now();s->stop();CHECK(std::chrono::steady_clock::now()-t<std::chrono::seconds(1));s->stop();assert_clean();
 for(int mode=0;mode<5;++mode){reset();wrong_layout=mode==0;fail_frequency=mode==1;fail_buffer=mode==2;
  if(mode==3)format.repeat=2;if(mode==4)format.length=32;
  CHECK(!s->start(0,[](const float*,int){},err));CHECK(!err.empty());CHECK(!s->running());assert_clean();}
 reset();s->setUri("ip:missing");CHECK(!s->start(0,[](const float*,int){},err));assert_clean();
 s->setUri("ip:192.168.2.1");s->setSampleRate(NAN);CHECK(!s->start(0,[](const float*,int){},err));assert_clean();
 s->setSampleRate(2500000);fail_stream=true;
 {iridi::LiveIqSource live(iridi::SdrKind::IIO);live.set_uri("ip:192.168.2.1");
  CHECK(live.open(0,1621250000,2500000,err));float out[8];CHECK(live.read_cf32(out,4)==0);CHECK(!live.is_open());CHECK(!live.last_error().empty());live.close();}
 assert_clean();reset();
 // Restart after a receive-thread failure; destructor must also cancel refill.
 CHECK(s->start(0,[](const float*,int){},err));s.reset();assert_clean();
 std::cout<<"PASS: discovery, stable indices, RX-only setup, shifted/padded IQ, rate readback, gain/AGC, cancel, failure cleanup, disconnect and restart\n";
 return 0;
 }catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
