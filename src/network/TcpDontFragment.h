#pragma once

// Force the IPv4 "Don't Fragment" flag on outgoing TCP segments of the WiFi station interface.
//
// lwIP never sets DF. Phone hotspots on IPv6-only mobile networks translate our IPv4 into IPv6
// (464xlat) and that path drops TCP segments without DF while UDP passes: on an Asus Zenfone 10
// on Orange/Sosh every TCP connect timed out although DNS and NTP worked, and a Windows PC on the
// same hotspot failed the same way as soon as DF was cleared on its TCP sockets.
//
// The lwIP netif output hook is replaced at runtime, so no rebuild of the SDK is needed. Cost is
// one branch and a 20-byte header checksum per outgoing TCP segment. Idempotent; call it after
// every station connect because esp_netif re-creates the lwIP netif when WiFi is turned off/on.
void applyTcpDontFragment();
