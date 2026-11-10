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
#include <windows.h>
#include <wincred.h>
#endif

#include <string.h>

#include <vector>

#include <core/LogWriter.h>

#include "CredentialStore.h"

static core::LogWriter vlog("CredentialStore");

#ifdef WIN32

static const wchar_t* TARGET_PREFIX = L"TigerVNC:";

static std::wstring toWide(const std::string& s)
{
  int len;
  std::wstring result;

  if (s.empty())
    return result;

  len = MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), nullptr, 0);
  if (len <= 0)
    return result;

  result.resize(len);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), &result[0], len);
  return result;
}

static std::string fromWide(const wchar_t* s)
{
  int len;
  std::string result;

  if (s == nullptr || *s == L'\0')
    return result;

  len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0,
                            nullptr, nullptr);
  if (len <= 1)
    return result;

  result.resize(len - 1);
  WideCharToMultiByte(CP_UTF8, 0, s, -1, &result[0], len,
                      nullptr, nullptr);
  return result;
}

static std::wstring targetName(const std::string& key)
{
  return std::wstring(TARGET_PREFIX) + toWide(key);
}

bool CredentialStore::available()
{
  return true;
}

bool CredentialStore::load(const std::string& key, std::string* user,
                           std::string* password)
{
  PCREDENTIALW cred;
  std::wstring target;

  target = targetName(key);
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred)) {
    DWORD err = GetLastError();
    if (err != ERROR_NOT_FOUND)
      vlog.error("Failed to read stored credentials: error %lu",
                 (unsigned long)err);
    return false;
  }

  if (user)
    *user = fromWide(cred->UserName);
  if (password) {
    password->assign((const char*)cred->CredentialBlob,
                     cred->CredentialBlobSize);
  }

  SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
  CredFree(cred);

  return true;
}

bool CredentialStore::store(const std::string& key, const std::string& user,
                            const std::string& password)
{
  CREDENTIALW cred;
  std::wstring target, wuser, comment;
  std::vector<BYTE> blob(password.begin(), password.end());

  if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
    vlog.error("Password is too long to store");
    return false;
  }

  target = targetName(key);
  wuser = toWide(user);
  comment = L"TigerVNC Viewer login";

  memset(&cred, 0, sizeof(cred));
  cred.Type = CRED_TYPE_GENERIC;
  cred.TargetName = &target[0];
  cred.Comment = &comment[0];
  cred.CredentialBlobSize = blob.size();
  cred.CredentialBlob = blob.empty() ? nullptr : blob.data();
  cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
  cred.UserName = wuser.empty() ? nullptr : &wuser[0];

  if (!CredWriteW(&cred, 0)) {
    vlog.error("Failed to store credentials: error %lu",
               (unsigned long)GetLastError());
    SecureZeroMemory(blob.data(), blob.size());
    return false;
  }

  SecureZeroMemory(blob.data(), blob.size());
  vlog.info("Stored login for %s", key.c_str());
  return true;
}

bool CredentialStore::forget(const std::string& key)
{
  std::wstring target;

  target = targetName(key);
  if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
    if (GetLastError() != ERROR_NOT_FOUND)
      vlog.error("Failed to delete stored credentials: error %lu",
                 (unsigned long)GetLastError());
    return false;
  }

  vlog.info("Forgot login for %s", key.c_str());
  return true;
}

bool CredentialStore::exists(const std::string& key)
{
  PCREDENTIALW cred;
  std::wstring target;

  target = targetName(key);
  if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred))
    return false;

  SecureZeroMemory(cred->CredentialBlob, cred->CredentialBlobSize);
  CredFree(cred);
  return true;
}

#else

// FIXME: libsecret (Linux) and Keychain (macOS) support

bool CredentialStore::available()
{
  return false;
}

bool CredentialStore::load(const std::string&, std::string*, std::string*)
{
  return false;
}

bool CredentialStore::store(const std::string&, const std::string&,
                            const std::string&)
{
  return false;
}

bool CredentialStore::forget(const std::string&)
{
  return false;
}

bool CredentialStore::exists(const std::string&)
{
  return false;
}

#endif
