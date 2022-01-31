#include "MacOSNetworkTapInterface.hh"

// TODO: this should come from one of the headers below, right? why doesn't it?
struct prf_ra {
  u_char onlink : 1;
  u_char autonomous : 1;
  u_char reserved : 6;
} prf_ra;

#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/bpf.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <net/ndrv.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>

#include <phosg/Network.hh>
#include <phosg/Process.hh>

using namespace std;



struct arp {
  uint16_t hardware_type;
  uint16_t protocol_type; // same as ether_type
  uint8_t hwaddr_len;
  uint8_t paddr_len;
  uint16_t operation;
};

static ssize_t get_ether_frame_size(uint16_t ether_type, const void* data, size_t size) {
  switch (ether_type) {
    case 0x0800: { // IPv4
      if (size < sizeof(ip)) {
        return 0;
      }
      return ntohs(reinterpret_cast<const ip*>(data)->ip_len);
    }

    case 0x86DD: { // IPv6
      if (size < sizeof(ip6_hdr)) {
        return 0;
      }
      return ntohs(reinterpret_cast<const ip6_hdr*>(data)->ip6_ctlun.ip6_un1.ip6_un1_plen);
    }

    case 0x0806: { // ARP
      if (size < sizeof(arp)) {
        return 0;
      }
      const arp* arp_header = reinterpret_cast<const arp*>(data);
      return sizeof(arp) + 2 * (arp_header->hwaddr_len + arp_header->paddr_len);
    }

    case 0x8100: { // VLAN tag
      if (size < 4) {
        return 0;
      }
      uint16_t subtype = reinterpret_cast<const uint16_t*>(data)[1];
      ssize_t subsize = get_ether_frame_size(
          subtype,
          reinterpret_cast<const char*>(data) + 4,
          size - 4);
      return (subsize > 0) ? (4 + subsize) : subsize;
    }

    // Some less-common protocols that we might want to support:
    case 0x8035: // RARP
      break; // TODO
    case 0x809B: // AppleTalk
      break; // TODO
    case 0x80F3: // AppleTalk ARP
      break; // TODO
    case 0x8137: // IPX
      break; // TODO
    case 0x9000: // loopback
      break; // TODO
  }

  return -1;
}

ssize_t MacOSNetworkTapInterface::get_frame_size(const void* data, size_t size) {
  if (size < sizeof(ether_header)) {
    return 0;
  }

  const ether_header* eth = reinterpret_cast<const ether_header*>(data);
  ssize_t subsize = get_ether_frame_size(
      ntohs(eth->ether_type),
      eth + 1,
      size - sizeof(ether_header));
  return (subsize > 0) ? (sizeof(ether_header) + subsize) : subsize;
}



MacOSNetworkTapInterface::MacOSNetworkTapInterface(
    uint8_t mac_address[6],
    uint8_t ip_address[4],
    ssize_t network_device_number,
    ssize_t io_device_number,
    size_t mtu,
    size_t metric,
    bool enable_nud,
    bool enable_router_advertisements,
    const char* ifconfig_command)
  : network_device_number(network_device_number),
    io_device_number(io_device_number),
    mtu(mtu),
    metric(metric),
    enable_nud(enable_nud),
    enable_router_advertisements(enable_router_advertisements),
    ifconfig_command(ifconfig_command) {

  memcpy(this->mac_address, mac_address, 6);
  memcpy(this->ip_address, ip_address, 4);
}

void MacOSNetworkTapInterface::open() {
  if (getuid() != 0) {
    throw runtime_error("insufficient permissions");
  }

  this->ndrv_fd = socket(AF_NDRV, SOCK_RAW, 0);
  if (!this->ndrv_fd.is_open()) {
    throw runtime_error(string_printf("cannot open network driver socket (%d)",
        errno));
  }

  this->io_device_name = string_printf("feth%zd", this->io_device_number);
  this->network_device_name = string_printf("feth%zd", this->network_device_number);
  run_process({this->ifconfig_command, this->io_device_name, "create"});
  run_process({this->ifconfig_command, this->network_device_name, "create"});

  {
    string mac = string_printf("%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
        this->mac_address[0], this->mac_address[1], this->mac_address[2],
        this->mac_address[3], this->mac_address[4], this->mac_address[5]);
    run_process({this->ifconfig_command, this->network_device_name, "lladdr", mac});
    string ip = string_printf("%02hhu.%02hhu.%02hhu.%02hhu",
        this->ip_address[0], this->ip_address[1], this->ip_address[2], this->ip_address[3]);
    run_process({this->ifconfig_command, this->network_device_name, ip});
  }

  run_process({this->ifconfig_command, this->io_device_name, "peer", this->network_device_name});
  run_process({this->ifconfig_command, this->io_device_name, "mtu", "16370", "up"});
  run_process({
      this->ifconfig_command,
      this->network_device_name,
      "mtu", string_printf("%zu", this->mtu),
      "metric", string_printf("%zu", this->metric),
      "up"});

  usleep(100000);
  {
    scoped_fd s(socket(AF_INET6, SOCK_DGRAM, 0));
    if (!s.is_open()) {
      fprintf(stderr, "warning: cannot create IPv6 socket for setting flags\n");

    } else {
      struct in6_ndireq nd;
      memset(&nd, 0, sizeof(nd));
      strncpy(nd.ifname, this->network_device_name.c_str(), sizeof(nd.ifname));

      if (ioctl(s, SIOCGIFINFO_IN6, &nd)) {
        fprintf(stderr, "warning: cannot get IPv6 behavior flags\n");

      } else {
        uint32_t orig_flags = reinterpret_cast<uint32_t>(nd.ndi.flags);
        if (enable_nud) {
          nd.ndi.flags |= ND6_IFF_PERFORMNUD;
        } else {
          nd.ndi.flags &= ~ND6_IFF_PERFORMNUD;
        }

        if (orig_flags != reinterpret_cast<uint32_t>(nd.ndi.flags) && ioctl(s, SIOCSIFINFO_FLAGS, &nd)) {
          fprintf(stderr,
              "warning: cannot %s IPv6 neighbor unreachability detection (%d)\n",
              enable_nud ? "enable" : "disable", errno);

        } else {
          struct in6_ifreq ifr;
          memset(&ifr, 0, sizeof(ifr));
          strncpy(ifr.ifr_name, this->network_device_name.c_str(), sizeof(ifr.ifr_name));
          uint32_t ioctl_request = enable_router_advertisements ?
              _IOWR('i', 132, struct in6_ifreq) :
              _IOWR('i', 133, struct in6_ifreq);
          if (ioctl(s, ioctl_request, &ifr)) {
            fprintf(stderr,
                "warning: cannot %s IPv6 router advertisements (%d)\n",
                enable_router_advertisements ? "enable" : "disable", errno);
          }
        }
      }
    }
  }

  struct sockaddr_ndrv nd;
  memset(&nd, 0, sizeof(nd));
  nd.snd_len = sizeof(struct sockaddr_ndrv);
  nd.snd_family = AF_NDRV;
  if (this->io_device_name.size() + 1 > sizeof(nd.snd_name)) {
    throw runtime_error(string_printf(
        "device name is too long: %s (must be %zu bytes or shorter)",
        io_device_name.c_str(), sizeof(nd.snd_name) - 1));
  }
  memcpy(nd.snd_name, this->io_device_name.data(), this->io_device_name.size());
  if (::bind(this->ndrv_fd, reinterpret_cast<struct sockaddr*>(&nd), sizeof(nd)) != 0) {
    throw runtime_error(string_printf("cannot bind network driver socket (%d)",
        errno));
  }
  if (connect(this->ndrv_fd, reinterpret_cast<struct sockaddr*>(&nd), sizeof(nd)) != 0) {
    throw runtime_error(string_printf(
        "cannot connect network driver socket (%d)", errno));
  }

  for (size_t x = 0; !this->bpf_fd.is_open(); x++) {
    string device_name = string_printf("/dev/bpf%zu", x);
    try {
      this->bpf_fd.open(device_name.c_str(), O_RDWR);
      break;
    } catch (const cannot_open_file&) { }
  }

  {
    unsigned long flags = 128 * 1024;
    if (ioctl(this->bpf_fd, BIOCSBLEN, &flags) != 0) {
      throw runtime_error(string_printf("cannot set receive buffer size (%d)",
          errno));
    }
    this->max_read_size = flags;

    flags = 1;
    if (ioctl(this->bpf_fd, BIOCIMMEDIATE, &flags) != 0) {
      throw runtime_error(string_printf("cannot enable immediate mode (%d)",
          errno));
    }

    flags = 0;
    if (ioctl(this->bpf_fd, BIOCSSEESENT, &flags) != 0) {
      throw runtime_error(string_printf(
          "cannot disable sent frame availability (%d)", errno));
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    if (this->io_device_name.size() + 1 > sizeof(ifr.ifr_name)) {
      throw runtime_error("I/O device name is too long");
    }
    memcpy(ifr.ifr_name, this->io_device_name.c_str(), this->io_device_name.size());
    if (ioctl(this->bpf_fd, BIOCSETIF, &ifr) != 0) {
      throw runtime_error(string_printf("cannot attach to interface (%d)",
          errno));
    }

    flags = 1;
    if (ioctl(this->bpf_fd, BIOCSHDRCMPLT, &flags) != 0) {
      throw runtime_error(string_printf(
          "cannot enable header autocomplete (%d)", errno));
    }

    flags = 1;
    if (ioctl(this->bpf_fd, BIOCPROMISC, &flags) != 0) {
      throw runtime_error(string_printf("cannot enable promiscuous mode (%d)",
          errno));
    }
  }

  this->poll.add(bpf_fd, POLLIN);
}

std::string MacOSNetworkTapInterface::recv(int timeout_ms) {
  if (this->received_frames.empty()) {
    auto ready_fds = this->poll.poll(timeout_ms);
    if (ready_fds.count(this->bpf_fd)) {
      this->on_data_available();
    }
  }

  if (!this->received_frames.empty()) {
    auto frame = this->received_frames.front();
    this->received_frames.pop_front();
    return frame;
  }
  return "";
}

void MacOSNetworkTapInterface::send(const std::string& data) {
  this->send(data.data(), data.size());
}

void MacOSNetworkTapInterface::send(const void* data, size_t size) {
  writex(this->ndrv_fd, data, size);
}

Poll& MacOSNetworkTapInterface::get_poll() {
  return this->poll;
}

int MacOSNetworkTapInterface::get_fd() {
  return this->bpf_fd;
}

void MacOSNetworkTapInterface::on_data_available() {
  string receive_buffer(this->max_read_size, '\0');
  ssize_t size = read(this->bpf_fd, const_cast<char*>(receive_buffer.data()),
      this->max_read_size);
  if (size < 0) {
    throw runtime_error(string_printf("read error from network interface (%d)", errno));
  } else if (size == 0) {
    throw runtime_error("network interface was closed");
  } else {
    for (ssize_t offset = 0; offset < size;) {
      const bpf_hdr* header = reinterpret_cast<const bpf_hdr*>(
          receive_buffer.data() + offset);

      if ((header->bh_caplen > 0) &&
          (offset + header->bh_hdrlen + header->bh_caplen <= size)) {
        const char* data = receive_buffer.data() + offset + header->bh_hdrlen;
        uint16_t frame_size = header->bh_caplen;
        this->received_frames.emplace_back(data, frame_size);
      }
      offset += BPF_WORDALIGN(header->bh_hdrlen + header->bh_caplen);
    }
  }
}

MacOSNetworkTapInterface::~MacOSNetworkTapInterface() {
  if (this->ndrv_fd.is_open()) {
    this->ndrv_fd.close();
  }
  if (this->bpf_fd.is_open()) {
    this->poll.remove(bpf_fd);
    this->bpf_fd.close();
  }
  if (!this->network_device_name.empty()) {
    run_process({this->ifconfig_command, this->network_device_name, "destroy"});
  }
  if (!this->io_device_name.empty()) {
    run_process({this->ifconfig_command, this->io_device_name, "destroy"});
  }
}
