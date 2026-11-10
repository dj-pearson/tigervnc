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

#include <stdio.h>

#include <string>

#include <gtest/gtest.h>

#include "ConnectionBook.h"

static std::string tempPath(const char* name)
{
  return ::testing::TempDir() + name;
}

TEST(ConnectionBook, addAssignsIdAndName)
{
  ConnectionBook book;
  ConnectionEntry e;

  e.host = "pc.example.com";
  const ConnectionEntry& added = book.add(e);

  EXPECT_FALSE(added.id.empty());
  EXPECT_EQ(added.name, "pc.example.com");
  EXPECT_EQ(book.entries().size(), 1u);
}

TEST(ConnectionBook, addKeepsIdsUnique)
{
  ConnectionBook book;
  ConnectionEntry e;

  e.id = "abc";
  e.host = "a";
  book.add(e);
  e.host = "b";
  book.add(e);

  ASSERT_EQ(book.entries().size(), 2u);
  EXPECT_NE(book.entries()[0].id, book.entries()[1].id);
}

TEST(ConnectionBook, addStripsLineBreaks)
{
  ConnectionBook book;
  ConnectionEntry e;

  e.name = "evil\nHost=other";
  e.host = " host\r\n";
  const ConnectionEntry& added = book.add(e);

  EXPECT_EQ(added.name, "evilHost=other");
  EXPECT_EQ(added.host, "host");
}

TEST(ConnectionBook, roundTrip)
{
  ConnectionBook book, loaded;
  ConnectionEntry e;
  std::string path;

  e.name = "Office PC";
  e.host = "office:1";
  e.username = "alice";
  e.group = "Work";
  e.favorite = true;
  e.lastConnected = 1700000000;
  e.connectCount = 7;
  std::string id = book.add(e).id;

  e = ConnectionEntry();
  e.host = "home";
  book.add(e);

  path = tempPath("roundtrip.conf");
  book.saveTo(path);
  loaded.loadFrom(path);
  remove(path.c_str());

  ASSERT_EQ(loaded.entries().size(), 2u);

  ConnectionEntry* office = loaded.findById(id);
  ASSERT_NE(office, nullptr);
  EXPECT_EQ(office->name, "Office PC");
  EXPECT_EQ(office->host, "office:1");
  EXPECT_EQ(office->username, "alice");
  EXPECT_EQ(office->group, "Work");
  EXPECT_TRUE(office->favorite);
  EXPECT_EQ(office->lastConnected, 1700000000);
  EXPECT_EQ(office->connectCount, 7u);

  EXPECT_NE(loaded.findByName("home"), nullptr);
}

TEST(ConnectionBook, loadMissingFileIsEmpty)
{
  ConnectionBook book;

  book.loadFrom(tempPath("does-not-exist.conf"));
  EXPECT_TRUE(book.entries().empty());
}

TEST(ConnectionBook, loadSkipsBrokenEntries)
{
  ConnectionBook book;
  std::string path;
  FILE* f;

  path = tempPath("broken.conf");
  f = fopen(path.c_str(), "w");
  ASSERT_NE(f, nullptr);
  fputs("# comment\n"
        "Host=orphan\n"
        "[a]\n"
        "Name=No host\n"
        "[b]\n"
        "Host=good\n"
        "favorite=true\n"
        "[b]\n"
        "Host=duplicate\n"
        "[c\n"
        "Host=bad section\n", f);
  fclose(f);

  book.loadFrom(path);
  remove(path.c_str());

  ASSERT_EQ(book.entries().size(), 1u);
  EXPECT_EQ(book.entries()[0].id, "b");
  EXPECT_EQ(book.entries()[0].host, "good");
  EXPECT_TRUE(book.entries()[0].favorite);
}

TEST(ConnectionBook, findByName)
{
  ConnectionBook book;
  ConnectionEntry e;

  e.name = "Living Room";
  e.host = "10.0.0.5";
  book.add(e);

  EXPECT_NE(book.findByName("living room"), nullptr);
  EXPECT_NE(book.findByName("  LIVING ROOM "), nullptr);
  EXPECT_EQ(book.findByName("living"), nullptr);
  EXPECT_EQ(book.findByName(""), nullptr);
}

TEST(ConnectionBook, findByHost)
{
  ConnectionBook book;
  ConnectionEntry e;

  e.host = "Server";
  book.add(e);

  EXPECT_NE(book.findByHost("Server"), nullptr);
  EXPECT_NE(book.findByHost("server:0"), nullptr);
  EXPECT_NE(book.findByHost("server::5900"), nullptr);
  EXPECT_EQ(book.findByHost("server:1"), nullptr);
  EXPECT_EQ(book.findByHost("other"), nullptr);
}

TEST(ConnectionBook, removeAndTouch)
{
  ConnectionBook book;
  ConnectionEntry e;

  e.host = "a";
  std::string id = book.add(e).id;

  book.touch(id, 1234);
  book.touch(id, 5678);
  EXPECT_EQ(book.findById(id)->lastConnected, 5678);
  EXPECT_EQ(book.findById(id)->connectCount, 2u);

  EXPECT_TRUE(book.remove(id));
  EXPECT_FALSE(book.remove(id));
  EXPECT_TRUE(book.entries().empty());
}

TEST(ConnectionBook, groups)
{
  ConnectionBook book;
  ConnectionEntry e;

  e.host = "a"; e.group = "work"; book.add(e);
  e.host = "b"; e.group = "Home"; book.add(e);
  e.host = "c"; e.group = "work"; book.add(e);
  e.host = "d"; e.group = ""; book.add(e);

  std::vector<std::string> groups = book.groups();
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_EQ(groups[0], "Home");
  EXPECT_EQ(groups[1], "work");
}

TEST(ConnectionBook, credentialKey)
{
  EXPECT_EQ(ConnectionBook::credentialKey("PC"), "pc:5900");
  EXPECT_EQ(ConnectionBook::credentialKey("pc:0"), "pc:5900");
  EXPECT_EQ(ConnectionBook::credentialKey("pc::5900"), "pc:5900");
  EXPECT_EQ(ConnectionBook::credentialKey("pc:2"), "pc:5902");
  EXPECT_EQ(ConnectionBook::credentialKey("10.1.2.3::6000"),
            "10.1.2.3:6000");
}

TEST(ConnectionBook, sameServer)
{
  EXPECT_TRUE(sameServer("host", "HOST:0"));
  EXPECT_TRUE(sameServer("host:1", "host::5901"));
  EXPECT_FALSE(sameServer("host:1", "host"));
  EXPECT_FALSE(sameServer("", "host"));
}
