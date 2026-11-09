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

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <string.h>
#include <strings.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>

#include "TailscaleDiscovery.h"

static core::LogWriter vlog("Tailscale");

std::string TailscalePeer::vncAddress() const
{
  if (vncPort == 0 || vncPort == 5900)
    return ipv4;
  if (vncPort > 5900 && vncPort < 6000)
    return core::format("%s:%d", ipv4.c_str(), vncPort - 5900);
  return core::format("%s::%d", ipv4.c_str(), vncPort);
}

//
// Minimal JSON reader, just enough for "tailscale status --json"
//

namespace {

  struct JValue {
    enum Type { Null, Bool, Number, String, Array, Object };

    Type type = Null;
    bool boolean = false;
    double number = 0;
    std::string str;
    std::vector<JValue> array;
    std::vector<std::pair<std::string, JValue>> object;

    const JValue* get(const char* key) const {
      if (type != Object)
        return nullptr;
      for (const auto& member : object) {
        if (member.first == key)
          return &member.second;
      }
      return nullptr;
    }

    std::string getString(const char* key) const {
      const JValue* v = get(key);
      if (v == nullptr || v->type != String)
        return "";
      return v->str;
    }

    bool getBool(const char* key) const {
      const JValue* v = get(key);
      if (v == nullptr || v->type != Bool)
        return false;
      return v->boolean;
    }
  };

  class JParser {
  public:
    JParser(const std::string& text_) : text(text_), pos(0), depth(0) {}

    JValue parseDocument() {
      JValue v;

      v = parseValue();
      skipSpace();
      if (pos != text.size())
        fail("trailing data");
      return v;
    }

  private:
    [[noreturn]] void fail(const char* what) {
      throw std::runtime_error(
        core::format("Invalid JSON at offset %u: %s", (unsigned)pos, what));
    }

    void skipSpace() {
      while (pos < text.size() &&
             (text[pos] == ' ' || text[pos] == '\t' ||
              text[pos] == '\r' || text[pos] == '\n'))
        pos++;
    }

    bool consume(const char* literal) {
      size_t len = strlen(literal);
      if (text.compare(pos, len, literal) != 0)
        return false;
      pos += len;
      return true;
    }

    JValue parseValue() {
      JValue v;

      skipSpace();
      if (pos >= text.size())
        fail("unexpected end");

      if (++depth > 64)
        fail("nesting too deep");

      switch (text[pos]) {
      case '{':
        parseObject(&v);
        break;
      case '[':
        parseArray(&v);
        break;
      case '"':
        v.type = JValue::String;
        v.str = parseString();
        break;
      case 't':
        if (!consume("true"))
          fail("bad literal");
        v.type = JValue::Bool;
        v.boolean = true;
        break;
      case 'f':
        if (!consume("false"))
          fail("bad literal");
        v.type = JValue::Bool;
        v.boolean = false;
        break;
      case 'n':
        if (!consume("null"))
          fail("bad literal");
        break;
      default:
        parseNumber(&v);
        break;
      }

      depth--;
      return v;
    }

    void parseObject(JValue* v) {
      v->type = JValue::Object;
      pos++; // '{'

      skipSpace();
      if (pos < text.size() && text[pos] == '}') {
        pos++;
        return;
      }

      while (true) {
        std::string key;

        skipSpace();
        if (pos >= text.size() || text[pos] != '"')
          fail("expected key");
        key = parseString();

        skipSpace();
        if (pos >= text.size() || text[pos] != ':')
          fail("expected ':'");
        pos++;

        v->object.emplace_back(key, parseValue());

        skipSpace();
        if (pos >= text.size())
          fail("unexpected end");
        if (text[pos] == ',') {
          pos++;
          continue;
        }
        if (text[pos] == '}') {
          pos++;
          return;
        }
        fail("expected ',' or '}'");
      }
    }

    void parseArray(JValue* v) {
      v->type = JValue::Array;
      pos++; // '['

      skipSpace();
      if (pos < text.size() && text[pos] == ']') {
        pos++;
        return;
      }

      while (true) {
        v->array.push_back(parseValue());

        skipSpace();
        if (pos >= text.size())
          fail("unexpected end");
        if (text[pos] == ',') {
          pos++;
          continue;
        }
        if (text[pos] == ']') {
          pos++;
          return;
        }
        fail("expected ',' or ']'");
      }
    }

    unsigned parseHex4() {
      unsigned value = 0;

      if (pos + 4 > text.size())
        fail("short \\u escape");

      for (int i = 0; i < 4; i++) {
        char c = text[pos++];
        value <<= 4;
        if (c >= '0' && c <= '9')
          value |= c - '0';
        else if (c >= 'a' && c <= 'f')
          value |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
          value |= c - 'A' + 10;
        else
          fail("bad \\u escape");
      }

      return value;
    }

    static void appendUtf8(std::string* out, unsigned cp) {
      if (cp < 0x80) {
        *out += (char)cp;
      } else if (cp < 0x800) {
        *out += (char)(0xc0 | (cp >> 6));
        *out += (char)(0x80 | (cp & 0x3f));
      } else if (cp < 0x10000) {
        *out += (char)(0xe0 | (cp >> 12));
        *out += (char)(0x80 | ((cp >> 6) & 0x3f));
        *out += (char)(0x80 | (cp & 0x3f));
      } else {
        *out += (char)(0xf0 | (cp >> 18));
        *out += (char)(0x80 | ((cp >> 12) & 0x3f));
        *out += (char)(0x80 | ((cp >> 6) & 0x3f));
        *out += (char)(0x80 | (cp & 0x3f));
      }
    }

    std::string parseString() {
      std::string out;

      pos++; // '"'

      while (true) {
        char c;

        if (pos >= text.size())
          fail("unterminated string");

        c = text[pos++];
        if (c == '"')
          return out;

        if (c != '\\') {
          out += c;
          continue;
        }

        if (pos >= text.size())
          fail("unterminated escape");

        c = text[pos++];
        switch (c) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned cp = parseHex4();
          if (cp >= 0xd800 && cp < 0xdc00 &&
              text.compare(pos, 2, "\\u") == 0) {
            unsigned low;
            pos += 2;
            low = parseHex4();
            if (low >= 0xdc00 && low < 0xe000)
              cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            else
              cp = 0xfffd;
          } else if (cp >= 0xd800 && cp < 0xe000) {
            cp = 0xfffd;
          }
          appendUtf8(&out, cp);
          break;
        }
        default:
          fail("bad escape");
        }
      }
    }

    void parseNumber(JValue* v) {
      size_t start = pos;

      while (pos < text.size() &&
             strchr("+-0123456789.eE", text[pos]) != nullptr)
        pos++;

      if (pos == start)
        fail("unexpected character");

      v->type = JValue::Number;
      v->number = strtod(text.substr(start, pos - start).c_str(), nullptr);
    }

  private:
    const std::string& text;
    size_t pos;
    int depth;
  };

}

static TailscalePeer peerFromJson(const JValue& node, bool self)
{
  TailscalePeer peer;
  const JValue* ips;

  peer.hostName = node.getString("HostName");
  peer.dnsName = node.getString("DNSName");
  if (!peer.dnsName.empty() && peer.dnsName.back() == '.')
    peer.dnsName.pop_back();
  peer.os = node.getString("OS");
  peer.online = node.getBool("Online");
  peer.self = self;

  ips = node.get("TailscaleIPs");
  if (ips != nullptr && ips->type == JValue::Array) {
    for (const JValue& ip : ips->array) {
      if (ip.type == JValue::String &&
          ip.str.find(':') == std::string::npos) {
        peer.ipv4 = ip.str;
        break;
      }
    }
  }

  if (peer.hostName.empty()) {
    peer.hostName = peer.dnsName.substr(0, peer.dnsName.find('.'));
  }

  return peer;
}

std::vector<TailscalePeer>
TailscaleDiscovery::parseStatus(const std::string& json)
{
  JValue root;
  const JValue* self;
  const JValue* peers;
  std::string state;
  std::vector<TailscalePeer> result;

  root = JParser(json).parseDocument();
  if (root.type != JValue::Object)
    throw std::runtime_error(_("Unexpected output from Tailscale"));

  state = root.getString("BackendState");
  if (!state.empty() && state != "Running")
    throw std::runtime_error(
      core::format(_("Tailscale is not connected (state: %s)"),
                   state.c_str()));

  self = root.get("Self");
  if (self != nullptr && self->type == JValue::Object)
    result.push_back(peerFromJson(*self, true));

  peers = root.get("Peer");
  if (peers != nullptr && peers->type == JValue::Object) {
    for (const auto& member : peers->object) {
      if (member.second.type != JValue::Object)
        continue;
      result.push_back(peerFromJson(member.second, false));
    }
  }

  // Drop anything we can't connect to
  result.erase(std::remove_if(result.begin(), result.end(),
                              [](const TailscalePeer& p) {
                                return p.ipv4.empty();
                              }),
               result.end());

  std::sort(result.begin(), result.end(),
            [](const TailscalePeer& a, const TailscalePeer& b) {
              return strcasecmp(a.hostName.c_str(),
                                b.hostName.c_str()) < 0;
            });

  return result;
}

//
// Running the tailscale CLI
//

#ifdef WIN32

static bool runCommand(const std::wstring& exe, std::string* output)
{
  SECURITY_ATTRIBUTES sa;
  HANDLE readPipe, writePipe;
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  std::wstring cmdline;
  DWORD exitCode;
  char buf[4096];
  DWORD n;

  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    return false;
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writePipe;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  cmdline = L"\"" + exe + L"\" status --json";

  if (!CreateProcessW(nullptr, &cmdline[0], nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(readPipe);
    CloseHandle(writePipe);
    return false;
  }

  CloseHandle(writePipe);

  output->clear();
  while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0)
    output->append(buf, n);

  CloseHandle(readPipe);

  WaitForSingleObject(pi.hProcess, 5000);
  if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    exitCode = 1;

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return exitCode == 0 || !output->empty();
}

static std::string runTailscaleStatus()
{
  std::string output;
  wchar_t programFiles[MAX_PATH];
  DWORD len;

  if (runCommand(L"tailscale.exe", &output))
    return output;

  len = GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
  if (len > 0 && len < MAX_PATH) {
    std::wstring exe = std::wstring(programFiles) +
                       L"\\Tailscale\\tailscale.exe";
    if (runCommand(exe, &output))
      return output;
  }

  throw std::runtime_error(_("Tailscale is not installed or could not "
                             "be started"));
}

#else

static bool runCommand(const char* cmd, std::string* output)
{
  FILE* f;
  char buf[4096];
  size_t n;
  int status;

  f = popen(cmd, "r");
  if (f == nullptr)
    return false;

  output->clear();
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    output->append(buf, n);

  status = pclose(f);
  return status == 0 || !output->empty();
}

static std::string runTailscaleStatus()
{
  std::string output;

  if (runCommand("tailscale status --json 2>/dev/null", &output))
    return output;

#ifdef __APPLE__
  if (runCommand("/Applications/Tailscale.app/Contents/MacOS/Tailscale "
                 "status --json 2>/dev/null", &output))
    return output;
#endif

  throw std::runtime_error(_("Tailscale is not installed or could not "
                             "be started"));
}

#endif

std::vector<TailscalePeer> TailscaleDiscovery::listPeers()
{
  std::string output;

  output = runTailscaleStatus();
  if (output.empty())
    throw std::runtime_error(_("Tailscale returned no data"));

  return parseStatus(output);
}

//
// Port probing
//

#ifdef WIN32
typedef SOCKET probe_socket_t;
#define PROBE_INVALID INVALID_SOCKET
#define probe_close closesocket
#else
typedef int probe_socket_t;
#define PROBE_INVALID (-1)
#define probe_close close
#endif

namespace {
  struct Probe {
    size_t peer;
    size_t portIndex;
    probe_socket_t sock;
  };
}

static probe_socket_t startConnect(const std::string& ip, int port)
{
  probe_socket_t sock;
  struct sockaddr_in addr;
  int ret;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
    return PROBE_INVALID;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == PROBE_INVALID)
    return PROBE_INVALID;

#ifdef WIN32
  u_long nonblocking = 1;
  ioctlsocket(sock, FIONBIO, &nonblocking);
#else
  fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
#endif

  ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
  if (ret == 0)
    return sock;

#ifdef WIN32
  if (WSAGetLastError() == WSAEWOULDBLOCK)
    return sock;
#else
  if (errno == EINPROGRESS)
    return sock;
#endif

  probe_close(sock);
  return PROBE_INVALID;
}

void TailscaleDiscovery::probeVncPorts(std::vector<TailscalePeer>* peers,
                                       const std::vector<int>& ports,
                                       int timeoutMs)
{
  std::vector<Probe> pending;
  std::vector<size_t> bestPort(peers->size(), ports.size());
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(timeoutMs);

#ifdef WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    return;
#endif

  for (size_t i = 0; i < peers->size(); i++) {
    const TailscalePeer& peer = (*peers)[i];

    if (!peer.online && !peer.self)
      continue;

    for (size_t j = 0; j < ports.size(); j++) {
      probe_socket_t sock = startConnect(peer.ipv4, ports[j]);
      if (sock != PROBE_INVALID)
        pending.push_back({i, j, sock});
    }
  }

  // FD_SETSIZE limits how many we can wait on at once
  while (!pending.empty()) {
    fd_set wfds, efds;
    struct timeval tv;
    long remaining;
    int maxfd = 0;
    size_t batch;
    int n;

    remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0)
      break;

    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    batch = std::min(pending.size(), (size_t)FD_SETSIZE - 1);
    for (size_t i = 0; i < batch; i++) {
      FD_SET(pending[i].sock, &wfds);
      FD_SET(pending[i].sock, &efds);
      maxfd = std::max(maxfd, (int)pending[i].sock);
    }

    tv.tv_sec = remaining / 1000;
    tv.tv_usec = (remaining % 1000) * 1000;

    n = select(maxfd + 1, nullptr, &wfds, &efds, &tv);
    if (n <= 0)
      break;

    for (size_t i = 0; i < batch; ) {
      Probe& probe = pending[i];
      bool done = false;

      if (FD_ISSET(probe.sock, &efds)) {
        done = true;
      } else if (FD_ISSET(probe.sock, &wfds)) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(probe.sock, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        if (err == 0 && probe.portIndex < bestPort[probe.peer])
          bestPort[probe.peer] = probe.portIndex;
        done = true;
      }

      if (done) {
        probe_close(probe.sock);
        pending[i] = pending.back();
        pending.pop_back();
        if (batch > pending.size())
          batch = pending.size();
      } else {
        i++;
      }
    }
  }

  for (const Probe& probe : pending)
    probe_close(probe.sock);

  for (size_t i = 0; i < peers->size(); i++) {
    if (bestPort[i] < ports.size()) {
      (*peers)[i].vncPort = ports[bestPort[i]];
      vlog.debug("%s has VNC on port %d", (*peers)[i].hostName.c_str(),
                 (*peers)[i].vncPort);
    }
  }

#ifdef WIN32
  WSACleanup();
#endif
}
