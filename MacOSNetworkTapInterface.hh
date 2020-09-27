#include <stdint.h>

#include <deque>
#include <memory>
#include <string>
#include <phosg/Process.hh>
#include <phosg/Filesystem.hh>

class MacOSNetworkTapInterface {
public:
  MacOSNetworkTapInterface(
      uint8_t mac_address[6],
      uint8_t ip_address[4],
      ssize_t network_device_number = -1,
      ssize_t io_device_number = -1,
      size_t mtu = 1500,
      size_t metric = 0,
      bool enable_nud = true,
      bool enable_router_advertisements = false,
      const char* ifconfig_command = "ifconfig");
  virtual ~MacOSNetworkTapInterface();

  void open();

  // For simple use cases, only send() and recv() are needed. These functions
  // should be self-explanatory - each call sends or receives exactly one frame.
  // If there are no frames available, recv() returns an empty string.
  void send(const std::string& data);
  void send(const void* data, size_t size);
  std::string recv(int timeout_ms);

  // For more advanced use cases, these functions provide access to the internal
  // I/O structures. This is useful for using the internal Poll object for file
  // descriptors outside of this class - make sure to call on_data_available if
  // you call poll.poll() elsewhere and it shows that 
  Poll& get_poll();
  int get_fd();
  void on_data_available();

  // Computes the size of the frame based on the contents and protocol.
  // Returns 0 if the header is incomplete; returns -1 if the protocol is
  // unsupported or the frame is corrupt.
  static ssize_t get_frame_size(const void* data, size_t size);

protected:
  // arguments
  ssize_t network_device_number;
  ssize_t io_device_number;
  uint8_t mac_address[6];
  uint8_t ip_address[4];
  size_t mtu;
  size_t metric;
  bool enable_nud;
  bool enable_router_advertisements;
  std::string ifconfig_command;

  // internal state
  scoped_fd bpf_fd;
  scoped_fd ndrv_fd;
  Poll poll;
  std::string network_device_name;
  std::string io_device_name;
  std::deque<std::string> received_frames;
  size_t max_read_size;
};
