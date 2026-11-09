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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <algorithm>
#include <random>
#include <stdexcept>

#include <core/Exception.h>
#include <core/string.h>
#include <core/xdgdirs.h>

#include <network/TcpSocket.h>

#include "ConnectionBook.h"

std::string activeConnectionId;

static const char* BOOK_FILE = "connections.conf";
static const char* BOOK_HEADER = "# TigerVNC saved connections";

static std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return tolower(c); });
  return s;
}

static std::string trim(const std::string& s)
{
  size_t start, end;

  start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos)
    return "";
  end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

// Values are stored one per line, so line breaks are not allowed
static std::string sanitize(const std::string& s)
{
  std::string result;

  for (char c : s) {
    if (c == '\r' || c == '\n')
      continue;
    result += c;
  }

  return trim(result);
}

bool sameServer(const std::string& a, const std::string& b)
{
  std::string hostA, hostB;
  int portA, portB;

  if (a.empty() || b.empty())
    return false;

#ifndef WIN32
  if ((a.find("/") != std::string::npos) ||
      (b.find("/") != std::string::npos))
    return a == b;
#endif

  try {
    network::getHostAndPort(a.c_str(), &hostA, &portA);
    network::getHostAndPort(b.c_str(), &hostB, &portB);
  } catch (std::exception&) {
    return false;
  }

  return (toLower(hostA) == toLower(hostB)) && (portA == portB);
}

std::string ConnectionBook::defaultPath()
{
  const char* configDir;

  configDir = core::getvncconfigdir();
  if (configDir == nullptr)
    throw std::runtime_error("Could not determine VNC config directory path");

  return core::format("%s/%s", configDir, BOOK_FILE);
}

std::string ConnectionBook::optionsPath(const std::string& id)
{
  const char* configDir;
  std::string dir;

  configDir = core::getvncconfigdir();
  if (configDir == nullptr)
    throw std::runtime_error("Could not determine VNC config directory path");

  dir = core::format("%s/connections", configDir);
  core::mkdir_p(dir.c_str(), 0755);

  return core::format("%s/%s.tigervnc", dir.c_str(), id.c_str());
}

std::string ConnectionBook::credentialKey(const std::string& host)
{
  std::string hostname;
  int port;

#ifndef WIN32
  if (host.find("/") != std::string::npos)
    return host;
#endif

  try {
    network::getHostAndPort(host.c_str(), &hostname, &port);
  } catch (std::exception&) {
    return toLower(trim(host));
  }

  return core::format("%s:%d", toLower(hostname).c_str(), port);
}

std::string ConnectionBook::newId()
{
  static std::mt19937_64 rng(std::random_device{}() ^
                             (uint64_t)time(nullptr));

  return core::format("%012llx",
                      (unsigned long long)(rng() & 0xffffffffffffULL));
}

void ConnectionBook::load()
{
  loadFrom(defaultPath());
}

void ConnectionBook::save() const
{
  saveTo(defaultPath());
}

void ConnectionBook::loadFrom(const std::string& path)
{
  FILE* f;
  char buf[4096];
  ConnectionEntry* current;

  entries_.clear();

  f = fopen(path.c_str(), "r");
  if (!f) {
    if (errno == ENOENT)
      return;
    throw core::posix_error(
      core::format("Failed to open \"%s\"", path.c_str()), errno);
  }

  current = nullptr;
  while (fgets(buf, sizeof(buf), f)) {
    std::string line, key, value;
    size_t eq;

    line = trim(buf);
    if (line.empty() || line[0] == '#' || line[0] == ';')
      continue;

    if (line[0] == '[') {
      size_t end = line.find(']');
      ConnectionEntry entry;

      if (end == std::string::npos)
        continue;

      entry.id = trim(line.substr(1, end - 1));
      if (entry.id.empty() || findById(entry.id) != nullptr) {
        current = nullptr;
        continue;
      }

      entries_.push_back(entry);
      current = &entries_.back();
      continue;
    }

    if (current == nullptr)
      continue;

    eq = line.find('=');
    if (eq == std::string::npos)
      continue;

    key = toLower(trim(line.substr(0, eq)));
    value = trim(line.substr(eq + 1));

    if (key == "name")
      current->name = value;
    else if (key == "host")
      current->host = value;
    else if (key == "username")
      current->username = value;
    else if (key == "group")
      current->group = value;
    else if (key == "favorite")
      current->favorite = (value == "1" || toLower(value) == "true");
    else if (key == "lastconnected")
      current->lastConnected = (time_t)strtoll(value.c_str(), nullptr, 10);
    else if (key == "connectcount")
      current->connectCount = strtoul(value.c_str(), nullptr, 10);
  }

  fclose(f);

  // An entry without a host is useless
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [](const ConnectionEntry& e) {
                                  return e.host.empty();
                                }),
                 entries_.end());
}

void ConnectionBook::saveTo(const std::string& path) const
{
  std::string tmpPath;
  FILE* f;

  tmpPath = path + ".tmp";

  f = fopen(tmpPath.c_str(), "w");
  if (!f)
    throw core::posix_error(
      core::format("Failed to open \"%s\"", tmpPath.c_str()), errno);

  fprintf(f, "%s\n", BOOK_HEADER);

  for (const ConnectionEntry& e : entries_) {
    fprintf(f, "\n[%s]\n", sanitize(e.id).c_str());
    fprintf(f, "Name=%s\n", sanitize(e.name).c_str());
    fprintf(f, "Host=%s\n", sanitize(e.host).c_str());
    if (!e.username.empty())
      fprintf(f, "Username=%s\n", sanitize(e.username).c_str());
    if (!e.group.empty())
      fprintf(f, "Group=%s\n", sanitize(e.group).c_str());
    fprintf(f, "Favorite=%d\n", e.favorite ? 1 : 0);
    fprintf(f, "LastConnected=%lld\n", (long long)e.lastConnected);
    fprintf(f, "ConnectCount=%u\n", e.connectCount);
  }

  if (fclose(f) != 0) {
    ::remove(tmpPath.c_str());
    throw core::posix_error(
      core::format("Failed to write \"%s\"", tmpPath.c_str()), errno);
  }

#ifdef WIN32
  if (!MoveFileExA(tmpPath.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    ::remove(tmpPath.c_str());
    throw std::runtime_error(
      core::format("Failed to replace \"%s\"", path.c_str()));
  }
#else
  if (rename(tmpPath.c_str(), path.c_str()) != 0) {
    int err = errno;
    ::remove(tmpPath.c_str());
    throw core::posix_error(
      core::format("Failed to replace \"%s\"", path.c_str()), err);
  }
#endif
}

ConnectionEntry* ConnectionBook::findById(const std::string& id)
{
  for (ConnectionEntry& e : entries_) {
    if (e.id == id)
      return &e;
  }
  return nullptr;
}

ConnectionEntry* ConnectionBook::findByName(const std::string& name)
{
  std::string needle;

  needle = toLower(trim(name));
  if (needle.empty())
    return nullptr;

  for (ConnectionEntry& e : entries_) {
    if (toLower(e.name) == needle)
      return &e;
  }
  return nullptr;
}

ConnectionEntry* ConnectionBook::findByHost(const std::string& host)
{
  for (ConnectionEntry& e : entries_) {
    if (e.host == host)
      return &e;
  }
  for (ConnectionEntry& e : entries_) {
    if (sameServer(e.host, host))
      return &e;
  }
  return nullptr;
}

ConnectionEntry& ConnectionBook::add(ConnectionEntry entry)
{
  if (entry.id.empty() || findById(entry.id) != nullptr) {
    do {
      entry.id = newId();
    } while (findById(entry.id) != nullptr);
  }

  entry.name = sanitize(entry.name);
  entry.host = sanitize(entry.host);
  entry.username = sanitize(entry.username);
  entry.group = sanitize(entry.group);
  if (entry.name.empty())
    entry.name = entry.host;

  entries_.push_back(entry);
  return entries_.back();
}

bool ConnectionBook::remove(const std::string& id)
{
  auto iter = std::find_if(entries_.begin(), entries_.end(),
                           [&id](const ConnectionEntry& e) {
                             return e.id == id;
                           });
  if (iter == entries_.end())
    return false;

  entries_.erase(iter);
  return true;
}

void ConnectionBook::touch(const std::string& id, time_t when)
{
  ConnectionEntry* e;

  e = findById(id);
  if (e == nullptr)
    return;

  e->lastConnected = when;
  e->connectCount++;
}

std::vector<std::string> ConnectionBook::groups() const
{
  std::vector<std::string> result;

  for (const ConnectionEntry& e : entries_) {
    if (e.group.empty())
      continue;
    if (std::find(result.begin(), result.end(), e.group) != result.end())
      continue;
    result.push_back(e.group);
  }

  std::sort(result.begin(), result.end(),
            [](const std::string& a, const std::string& b) {
              return toLower(a) < toLower(b);
            });

  return result;
}
