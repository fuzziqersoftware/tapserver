# tapserver

This program enables tap-like network interfaces on macOS, without installing any kernel extensions. This program can replace many uses of the [TunTap extension (tuntaposx)](http://tuntaposx.sourceforge.net/), which is no longer maintained. The mechanics of this program are based on [ZeroTier's MacEthernetTapAgent](https://github.com/zerotier/ZeroTierOne/blob/master/osdep/MacEthernetTapAgent.c).

When you run this program and a client program connects to it, the client essentially gets a stream socket connected directly to a network interface attached to the local machine. The client receives a stream of raw Ethernet frames over this socket, and can send frames to the host by simply writing them to the socket. This behavior is similar to how tap network interfaces work in Unix.

However, these socket connections aren't exactly the same as tap interfaces. There are a few behaviors to be aware of:
- Opening a stream socket to tapserver is done via socket() and connect() rather than via open().
- It is not necessarily true that each individual read() or write() call on a tapserver connection maps to a single packet. Using the framed protocol or using the library directly is recommended to solve this problem.

## Compiling

1. Build and install [phosg](https://github.com/fuzziqersoftware/phosg).
2. Run `make`. This will produce the library and the server executable.
3. Optionally, `sudo make install`. This is only necessary if you want the library and server on default paths.

## Usage

There are two ways to use tapserver: as a library or as a server.

### As a library

If you're the author of a program targeting the macOS platform and you want to use a tap interface, you can link with the included library. This library implements a class you can instantiate to directly read and write individual packets through a tap interface, removing the need for an intermediary. However, using the library requires elevated privileges, so it may be desirable to use the server anyway.

To use the library, create a MacOSNetworkTapInterface object and give it two unused feth device numbers (you can see if any feth devices already exist by running `ifconfig`). You'll also need to give it a MAC address and IP address; these apply to the host side of the connection. Once constructed, call open(); if open() doesn't throw, then the devices are created and ready. You can then call recv() and send() to read and write individual packets. (If recv() returns an empty string, there were no packets available within the timeout.) The interface object's destructor closes the stream and cleans up the system interfaces.

### As a server

Run `./tapserver` for detailed usage information. You'll probably need `sudo`.

The server can be used with existing software that uses a tap interface, provided that the software can be told to open a socket instead of /dev/tapN or can use a passed-in or inherited file descriptor. The server will wait for a connection, then open create and configure the network interface. It will forward data between the network interface and the stream socket bidirectionally until one is closed, at which point it will delete the network interface and exit.

The server has two different protocols: non-framed and framed. The non-framed protocol simply sends raw packets in both directions; the client and tapserver are individually responsible for figuring out the size of each frame if they need to know it. In this mode, tapserver can only understand a few protocols (IPv4, IPv6, and ARP), but more can be added in the future. The framed protocol does away with this problem by prepending a 16-bit size field in native byte order to each packet, but the client will have to be aware of this protocol change and act accordingly.

In general, you should use the framed protocol if either:
- You need to use any protocols that aren't IP or ARP
- The client sometimes sends incorrectly-sized packets (for example, garbage data after the end of an ARP packet)

#### Usage with Dolphin (GameCube/Wii emulator)

Go to Config -> GameCube and choose "Broadband Adapter (tapserver)" in the SP1 menu. Then run tapserver like this (replace 192.168.0.5 with the address you want to be assigned to the host, if needed):

    sudo ./tapserver --listen=/tmp/dolphin-tap --use-framed-protocol --ip-address=192.168.0.5

You'll have to configure the game's network settings appropriately. For connecting Phantasy Star Online to a locally-running proxy or private server, for example, you could use settings like this:
- Disable DHCP (manually set an IP address)
- IP address: 192.168.0.20
- Subnet mask: 255.255.255.0
- Default gateway: 192.168.0.5
- DNS server: 192.168.0.5

## Future

Some improvements I'd like to make in the future:
- Add support for more protocols, so the non-framed protocol will be more useful
- Find a way for clients to be able to use open() to connect
- Add a way to drop privileges (might be hard since we need to destroy the interface at exit time)
- Add a way to control the Unix socket's ownership and permissions

Please submit any issues via this GitHub repository's issues page.
