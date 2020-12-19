#include <stdint.h>
#include <sys/socket.h>

#include <string>
#include <phosg/Filesystem.hh>
#include <phosg/Network.hh>
#include <phosg/Process.hh>

#include "MacOSNetworkTapInterface.hh"

using namespace std;



bool should_exit = false;

void signal_handler(int signum) {
  should_exit = true;
}



void print_usage(FILE* stream, const char* argv0) {
  fprintf(stream, "\
Usage: %s --listen=<port | addr:port | socket path> [options]\n\
\n\
This program provides a stream socket connection to a network interface\n\
attached to the local machine. The client will receive a stream of packets over\n\
this socket, and can send packets to the host by simply writing to the socket.\n\
This behavior is similar to how tap network interfaces work in Unix.\n\
\n\
Options:\n\
  --help\n\
    You're reading it now.\n\
  --network-device-number=N\n\
    Use this device number for the host-side network interface. Usually this\n\
    number doesn\'t matter much; it just has to not already exist. (Default 1)\n\
  --io-device-number=N\n\
    Use this device number for the client-side network interface. Usually this\n\
    number doesn\'t matter much; it just has to not already exist. (Default 2)\n\
  --mac-address=XX:XX:XX:XX:XX:XX\n\
    Use this MAC address for the host-side network interface. (Default\n\
    90:90:90:90:90:90)\n\
  --ip-address=XXX.XXX.XXX.XXX\n\
    Use this IPv4 address for the host-side network interface. (Default\n\
    172.30.0.1)\n\
  --mtu=N\n\
    Set the MTU (maximum transmission unit) to this many bytes. (Default 1500)\n\
  --metric=N\n\
    Set the interface metric to this value. (Default 0)\n\
  --disable-nud\n\
    Disable IPv6 neighbor unavailability detection on this interface.\n\
  --enable-router-advertisements\n\
    Listen for IPv6 router advertisements on this interface.\n\
  --ifconfig-command=COMMAND\n\
    Use this command instead of the default ifconfig binary.\n\
  --listen=PORT\n\
    Listen for a client connection on this TCP port.\n\
  --listen=ADDR:PORT\n\
    Listen for a client connection on this TCP port on a specific interface.\n\
  --listen=PATH\n\
    Listen for a client connection on this Unix socket.\n\
  --show-data\n\
    Print a hex/ASCII dump of all frames sent and received over the interface.\n\
  --show-size-warnings\n\
    Print a hex/ASCII dump of all frames sent by the client for which tapserver\n\
    would compute the wrong frame size. This may be useful when testing with a\n\
    new use case, to determine if using the framed protocol is necessary.\n\
  --use-framed-protocol\n\
    Prepend each packet with a 2-byte, native-byte-order integer specifying its\n\
    size.\n\
\n", argv0);
}



int main(int argc, char** argv) {
  // tap interface options
  size_t network_device_number = 1;
  size_t io_device_number = 2;
  uint8_t mac_address[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  uint8_t ip_address[4] = {172, 30, 0, 1};
  size_t mtu = 1500;
  size_t metric = 0;
  bool enable_nud = true;
  bool enable_router_advertisements = false;
  const char* ifconfig_command = "ifconfig";
  // other options
  scoped_fd listen_fd;
  bool show_data = false;
  bool show_frame_size_warnings = false;
  bool use_framed_protocol = false;

  try {
    for (int x = 1; x < argc; x++) {
      if (!strcmp(argv[x], "--help")) {
        print_usage(stderr, argv[0]);
        return 0;
      } else if (!strncmp(argv[x], "--network-device-number=", 24)) {
        network_device_number = atoi(&argv[x][24]);
      } else if (!strncmp(argv[x], "--io-device-number=", 19)) {
        io_device_number = atoi(&argv[x][19]);
      } else if (!strncmp(argv[x], "--mac-address=", 14)) {
        string mac = parse_data_string(&argv[x][14]);
        if (mac.size() != 6) {
          throw invalid_argument("--mac-address must be 6 hexadecimal bytes");
        }
        memcpy(mac_address, mac.data(), 6);
      } else if (!strncmp(argv[x], "--ip-address=", 13)) {
        if (sscanf(&argv[x][13], "%hhu.%hhu.%hhu.%hhu", &ip_address[0],
            &ip_address[1], &ip_address[2], &ip_address[3]) != 4) {
          throw invalid_argument("--ip-address must be 4 decimal bytes");
        }
      } else if (!strncmp(argv[x], "--mtu=", 6)) {
        mtu = atoi(&argv[x][6]);
      } else if (!strncmp(argv[x], "--metric=", 9)) {
        metric = atoi(&argv[x][9]);
      } else if (!strcmp(argv[x], "--disable-nud")) {
        enable_nud = false;
      } else if (!strcmp(argv[x], "--enable-router-advertisements")) {
        enable_router_advertisements = true;
      } else if (!strncmp(argv[x], "--ifconfig-command=", 19)) {
        ifconfig_command = &argv[x][19];
      } else if (!strncmp(argv[x], "--listen=", 9)) {
        if (listen_fd.is_open()) {
          throw invalid_argument("--listen may only be given once");
        }
        auto parts = split(&argv[x][9], ':');
        if (parts.size() == 1) {
          if (!parts[0].empty() && (parts[0][0] == '/')) { // it's a unix socket
            listen_fd = ::listen(parts[0], 0, SOMAXCONN, false);
            fprintf(stderr, "listening on unix socket %s\n", parts[0].c_str());
            // TODO: make permissions configurable via CLI
            chmod(parts[0].c_str(), 0777);
          } else { // it's a port number
            int port = stoi(parts[0]);
            listen_fd = ::listen("", port, SOMAXCONN, false);
            fprintf(stderr, "listening on port %d\n", port);
          }
        } else if (parts.size() == 2) { // it's an addr:port pair
          int port = stoi(parts[1]);
          listen_fd = ::listen(parts[0], port, SOMAXCONN, false);
          fprintf(stderr, "listening on port %d\n", port);
        } else {
          throw invalid_argument("--listen must be an addr:port, port, or unix socket path");
        }
      } else if (!strcmp(argv[x], "--show-data")) {
        show_data = true;
      } else if (!strcmp(argv[x], "--show-size-warnings")) {
        show_frame_size_warnings = true;
      } else if (!strcmp(argv[x], "--use-framed-protocol")) {
        use_framed_protocol = true;
      } else {
        throw invalid_argument(string_printf("unknown option: %s", argv[x]));
      }
    }

    if (!listen_fd.is_open()) {
      throw invalid_argument("--listen must be given");
    }

  } catch (const invalid_argument& e) {
    fprintf(stderr, "invalid arguments: %s\n\n", e.what());
    print_usage(stderr, argv[0]);
  }

  // Wait for a client to connect
  fprintf(stderr, "waiting for connection\n");
  struct sockaddr_storage client_ss;
  socklen_t client_ss_size = sizeof(client_ss);
  scoped_fd client_fd = accept(
      listen_fd,
      reinterpret_cast<struct sockaddr*>(&client_ss),
      &client_ss_size);
  if (!client_fd.is_open()) {
    throw runtime_error(string_printf("could not accept client connection (%d)", errno));
  }
  fprintf(stderr, "client connected\n");
  listen_fd.close();

  signal(SIGQUIT, &signal_handler);
  signal(SIGTERM, &signal_handler);
  signal(SIGKILL, &signal_handler);
  signal(SIGINT, &signal_handler);
  signal(SIGPIPE, &signal_handler);

  MacOSNetworkTapInterface tap(
      mac_address,
      ip_address,
      network_device_number,
      io_device_number,
      mtu,
      metric,
      enable_nud,
      enable_router_advertisements,
      ifconfig_command);

  try {
    tap.open();

    Poll& poll = tap.get_poll();
    poll.add(client_fd, POLLIN);

    string read_buffer;
    while (!should_exit) {
      auto ready_fds = poll.poll();

      int tap_events = 0;
      try {
        tap_events = ready_fds.at(tap.get_fd());
      } catch (const out_of_range&) { }

      if (tap_events & POLLHUP) {
        fprintf(stderr, "tap disconnected\n");
        should_exit = true;
      } else if (tap_events & POLLIN) {
        tap.on_data_available();
        for (string frame = tap.recv(0); !frame.empty(); frame = tap.recv(0)) {
          ssize_t computed_size = MacOSNetworkTapInterface::get_frame_size(
              frame.data(), frame.size());
          if (show_frame_size_warnings && (computed_size != frame.size())) {
            fprintf(stderr,
                "\nWarning: outgoing frame size (0x%zX) would be incorrectly computed (0x%zX)\n",
                frame.size(), computed_size);
            print_data(stderr, frame);
          } else if (show_data) {
            fprintf(stderr, "\nTo tap client:\n");
            print_data(stderr, frame);
          }

          if (use_framed_protocol) {
            uint16_t size = frame.size();
            writex(client_fd, &size, sizeof(uint16_t));
          }
          writex(client_fd, frame);
        }
      }

      int client_events = 0;
      try {
        client_events = ready_fds.at(client_fd);
      } catch (const out_of_range&) { }

      if (client_events & POLLHUP) {
        fprintf(stderr, "client disconnected\n");
        should_exit = true;
      } else if (client_events & POLLIN) {
        read_buffer += read_all(client_fd);

        size_t offset;
        for (offset = 0; offset < read_buffer.size() - 2;) {
          ssize_t size;
          ssize_t skip_bytes;
          if (use_framed_protocol) {
            size = *reinterpret_cast<const uint16_t*>(read_buffer.data() + offset);
            skip_bytes = 2;
            size_t available_bytes = read_buffer.size() - offset - skip_bytes;
            ssize_t computed_size = MacOSNetworkTapInterface::get_frame_size(
                read_buffer.data() + offset + skip_bytes,
                available_bytes);
            if (computed_size != size) {
              fprintf(stderr,
                  "warning: frame size (0x%zX) would be incorrectly computed (0x%zX)\n",
                  size, computed_size);
              print_data(stderr, read_buffer.data() + offset + skip_bytes,
                  size > available_bytes ? available_bytes : size);
            }
          } else {
            size = tap.get_frame_size(read_buffer.data() + offset, read_buffer.size() - offset);
            if (size == 0) {
              break; // incomplete frame; need to read more data
            }
            if (size < 0) {
              print_data(stderr, read_buffer.data() + offset, read_buffer.size() - offset);
              throw runtime_error("cannot determine frame size");
            }
            skip_bytes = 0;
          }

          size_t end_offset = offset + skip_bytes + size;
          if (end_offset > read_buffer.size()) {
            break;
          }

          if (show_data) {
            fprintf(stderr, "\nFrom tap client:\n");
            print_data(stderr, read_buffer.data() + offset + skip_bytes, size);
          }
          tap.send(read_buffer.data() + offset + skip_bytes, size);
          offset = end_offset;
        }

        // Leave the incomplete frame in the read buffer (if any)
        read_buffer = read_buffer.substr(offset);
      }
    }
  } catch (const exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
    return 3;
  }

  return 0;
}
