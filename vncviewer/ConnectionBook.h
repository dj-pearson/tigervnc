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

#ifndef __CONNECTIONBOOK_H__
#define __CONNECTIONBOOK_H__

#include <stdint.h>
#include <time.h>

#include <string>
#include <vector>

// A named, saved connection ("address book" entry). The entry's
// viewer options live in a separate .tigervnc file next to the book,
// and its password (if any) lives in the OS credential store.
struct ConnectionEntry {
  std::string id;
  std::string name;
  std::string host;
  std::string username;
  std::string group;
  bool favorite = false;
  time_t lastConnected = 0;
  unsigned connectCount = 0;
};

class ConnectionBook {
public:
  // Loads/saves the book from/to the default location in the VNC
  // config directory
  void load();
  void save() const;

  // Explicit paths, mostly for testing
  void loadFrom(const std::string& path);
  void saveTo(const std::string& path) const;

  std::vector<ConnectionEntry>& entries() { return entries_; }
  const std::vector<ConnectionEntry>& entries() const { return entries_; }

  ConnectionEntry* findById(const std::string& id);
  // Case-insensitive match on the display name
  ConnectionEntry* findByName(const std::string& name);
  // Matches host strings that resolve to the same host and port
  ConnectionEntry* findByHost(const std::string& host);

  // Adds the entry, assigning a fresh id if it has none. Returns the
  // stored entry.
  ConnectionEntry& add(ConnectionEntry entry);
  bool remove(const std::string& id);

  // Records a successful connection attempt
  void touch(const std::string& id, time_t when);

  // Distinct, sorted, non-empty group names
  std::vector<std::string> groups() const;

  // Path of the per-entry viewer options file
  static std::string optionsPath(const std::string& id);
  static std::string defaultPath();

  // Key used for the credential store; equal for equivalent host
  // strings ("pc", "pc:0", "pc::5900")
  static std::string credentialKey(const std::string& host);

  static std::string newId();

private:
  std::vector<ConnectionEntry> entries_;
};

// True if both strings refer to the same host and port
bool sameServer(const std::string& a, const std::string& b);

// The saved connection used for the current session, or empty
extern std::string activeConnectionId;

#endif
