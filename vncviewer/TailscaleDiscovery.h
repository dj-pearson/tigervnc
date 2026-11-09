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

#ifndef __TAILSCALEDISCOVERY_H__
#define __TAILSCALEDISCOVERY_H__

#include <string>
#include <vector>

struct TailscalePeer {
  std::string hostName;
  std::string dnsName;   // MagicDNS name, without the trailing dot
  std::string ipv4;
  std::string os;
  bool online = false;
  bool self = false;
  int vncPort = 0;       // First open VNC port found, 0 if none

  // Host string suitable for the VNC server field
  std::string vncAddress() const;
};

namespace TailscaleDiscovery {

  // Runs "tailscale status --json" and returns the peers. Throws
  // std::runtime_error with a user presentable message on failure.
  std::vector<TailscalePeer> listPeers();

  // Parses the output of "tailscale status --json"
  std::vector<TailscalePeer> parseStatus(const std::string& json);

  // Probes the online peers for listening VNC ports, in parallel,
  // giving up after timeoutMs. Sets vncPort on the peers that answer.
  void probeVncPorts(std::vector<TailscalePeer>* peers,
                     const std::vector<int>& ports, int timeoutMs);

}

#endif
