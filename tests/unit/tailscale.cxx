/* Copyright 2026 dj-pearson
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdexcept>

#include <gtest/gtest.h>

#include "TailscaleDiscovery.h"

static const char* STATUS_JSON = R"json({
  "Version": "1.80.0",
  "BackendState": "Running",
  "Self": {
    "HostName": "desk",
    "DNSName": "desk.tail1234.ts.net.",
    "OS": "windows",
    "TailscaleIPs": ["100.64.0.1", "fd7a:115c:a1e0::1"],
    "Online": true
  },
  "Peer": {
    "nodekey:aaa": {
      "HostName": "raspberrypi",
      "DNSName": "raspberrypi.tail1234.ts.net.",
      "OS": "linux",
      "TailscaleIPs": ["fd7a:115c:a1e0::2", "100.64.0.2"],
      "Online": true,
      "Tags": null,
      "RxBytes": 12345,
      "Relay": "fra"
    },
    "nodekey:bbb": {
      "HostName": "Laptop \"work\" é",
      "DNSName": "laptop.tail1234.ts.net.",
      "OS": "macOS",
      "TailscaleIPs": ["100.64.0.3"],
      "Online": false,
      "LastSeen": "2026-01-01T00:00:00Z"
    },
    "nodekey:ccc": {
      "HostName": "v6only",
      "TailscaleIPs": ["fd7a:115c:a1e0::4"],
      "Online": true
    }
  },
  "User": {}
})json";

TEST(Tailscale, parseStatus)
{
  std::vector<TailscalePeer> peers;

  peers = TailscaleDiscovery::parseStatus(STATUS_JSON);

  // Sorted by name, IPv6-only peer dropped
  ASSERT_EQ(peers.size(), 3u);

  EXPECT_EQ(peers[0].hostName, "desk");
  EXPECT_TRUE(peers[0].self);
  EXPECT_EQ(peers[0].ipv4, "100.64.0.1");
  EXPECT_EQ(peers[0].dnsName, "desk.tail1234.ts.net");

  EXPECT_EQ(peers[1].hostName, "Laptop \"work\" \xc3\xa9");
  EXPECT_FALSE(peers[1].online);
  EXPECT_EQ(peers[1].os, "macOS");

  EXPECT_EQ(peers[2].hostName, "raspberrypi");
  EXPECT_EQ(peers[2].ipv4, "100.64.0.2");
  EXPECT_TRUE(peers[2].online);
  EXPECT_FALSE(peers[2].self);
}

TEST(Tailscale, notRunning)
{
  EXPECT_THROW(TailscaleDiscovery::parseStatus(
                 R"({"BackendState": "Stopped", "Peer": null})"),
               std::runtime_error);
}

TEST(Tailscale, invalidJson)
{
  EXPECT_THROW(TailscaleDiscovery::parseStatus(""), std::runtime_error);
  EXPECT_THROW(TailscaleDiscovery::parseStatus("{"), std::runtime_error);
  EXPECT_THROW(TailscaleDiscovery::parseStatus("{\"a\": tru}"),
               std::runtime_error);
  EXPECT_THROW(TailscaleDiscovery::parseStatus("{} extra"),
               std::runtime_error);
  EXPECT_THROW(TailscaleDiscovery::parseStatus("[1, 2]"),
               std::runtime_error);
  EXPECT_THROW(TailscaleDiscovery::parseStatus(std::string(1000, '[')),
               std::runtime_error);
}

TEST(Tailscale, emptyPeers)
{
  EXPECT_TRUE(TailscaleDiscovery::parseStatus(
                R"({"BackendState": "Running", "Peer": {}})").empty());
}

TEST(Tailscale, vncAddress)
{
  TailscalePeer p;

  p.ipv4 = "100.64.0.9";

  p.vncPort = 0;
  EXPECT_EQ(p.vncAddress(), "100.64.0.9");
  p.vncPort = 5900;
  EXPECT_EQ(p.vncAddress(), "100.64.0.9");
  p.vncPort = 5901;
  EXPECT_EQ(p.vncAddress(), "100.64.0.9:1");
  p.vncPort = 6100;
  EXPECT_EQ(p.vncAddress(), "100.64.0.9::6100");
}
