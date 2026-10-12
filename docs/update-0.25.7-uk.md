# Ribterra 0.25.7 — Addresses and connections across different networks

> English rendering of a historical project document, translated offline and checked for preservation of code, paths, identifiers, numeric literals, and recorded status markers. Historical records apply to their named snapshots and do not validate the current build. For exact original wording, see import commit `7388a2f`.

Fixed invitation address selection: active network adapter with gateway is displayed in front of virtual adapters without gateway. Previously, the button could copy the VirtualBox Host-Only address, which is not available to a second physical PC. The lobby now contains a list of adapters: select the desired LAN or VPN address before copying.

The message waiting for the host's response is separated from the world load. After the timeout, the game explains that you need to check the address, the room running and the availability of the host across the networks.

## Two computers on different networks

Ribterra uses a direct UDP- connection. There is no automatic Internet relay or NAT breakout in this release. Updating the address selection itself does not make a local address available over the Internet.

There are two possible ways:

1. Joint VPN: Both PCs are connected to the same private network, for example Radmin VPN. The host is building a room in Ribterra. The second player enters VPN- the host address and port, typically `:27020`. You need VPN- addresses, not Wi-Fi or VirtualBox.
2. Direct connection: the host has an external IP- address available; UDP-port 27020 is redirected to its local address, and the Windows Firewall allows Ribterra to receive this traffic. The second player enters an external IP- address with a port. For CGNAT One home router rule is not enough; You need an address from the provider or VPN.

On the host's PC, you need to press Create room» or Extend light» and leave it open. The "Create and Connect" screen itself does not yet launch the server.

## Direct connection through the router

The host router needs a Virtual Server/Port Forwarding rule: protocol **UDP**, external port **27020**, internal address — current LAN- address of the host PC, internal port **27020**. For a PC's stable address, you can use DHCP reservation. Allow input UDP 27020 for actual `Ribterra.exe` in the Windows Firewall.

The second field address of the server should be `host_public_IP:27020`. The addresses 192.168.x.x and VirtualBox are not routed between different networks via the public Internet. Compare the WAN- router address with the external connection address; The additional NAT requires the configuration of the previous router or the assistance of the provider. Success confirms Ribterra's connection to another network; The normal TCP- port check does not check this UDP- protocol.

For Zyxel USG FLEX in Nebula: Configure → Firewall → NAT → Virtual Server. The official [ Zyxel instruction](https://community.zyxel.com/en/discussion/14227/atp-flex-how-to-configure-a-nat-rule-virtual-server-on-nebula) explains the Public IP/Port, LAN IP/Port fields and the verification of the additional NAT.

The official instruction of Radmin VPN: https://www.radmin-vpn.com/help/

The storage and network protocol are compatible with 0.25.6. The parameters of the router, VPN and system firewall do not automatically change the game.
