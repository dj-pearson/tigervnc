/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
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

#ifndef __SERVERDIALOG_H__
#define __SERVERDIALOG_H__

#include <FL/Fl_Window.H>
#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "fltk/Fl_Suggestion_Input.h"
#include "ConnectionBook.h"
#include "TailscaleDiscovery.h"
#include "parameters.h"

class Fl_Box;
class Fl_Button;
class Fl_Input;
class Fl_Tree;
class Fl_Tree_Item;
class Fl_Widget;
class ConnectionTree;

class ServerDialog : public Fl_Window {
protected:
  ServerDialog();
  ~ServerDialog();

public:
  static std::string run(const char* servername);

protected:
  static void handleOptions(Fl_Widget *widget, void *data);
  static void handleLoad(Fl_Widget *widget, void *data);
  static void handleSaveAs(Fl_Widget *widget, void *data);
  static void handleAbout(Fl_Widget *widget, void *data);
  static void handleCancel(Fl_Widget *widget, void *data);
  static void handleConnect(Fl_Widget *widget, void *data);

  static void handleFilter(Fl_Widget *widget, void *data);
  static void handleTree(Fl_Widget *widget, void *data);
  static void handleAdd(Fl_Widget *widget, void *data);
  static void handleEdit(Fl_Widget *widget, void *data);
  static void handleDelete(Fl_Widget *widget, void *data);
  static void handleFavorite(Fl_Widget *widget, void *data);
  static void handleRefresh(Fl_Widget *widget, void *data);
  static void handleTreeActivate(void *data);

private:
  // What a row in the tree refers to
  struct ItemRef {
    enum Kind { Section, Entry, Recent, Tailscale };
    Kind kind;
    std::string id;    // Entry id
    std::string host;
    std::string label; // Suggested name (Tailscale)
  };

  void loadServerHistory();
  void saveServerHistory();
  void updateUsedDir(const char* filename);

  void loadBook();
  void saveBook();
  void rebuildTree();
  void addRow(Fl_Tree_Item* parent, const std::string& label,
              const ItemRef& ref);
  const ItemRef* selectedRef();
  void selectRef(const ItemRef& ref);
  void updateButtons();
  void applyEntryOptions(const std::string& id);
  void startTailscaleScan();

  static void checkTailscaleScan(void *data);

  static void onServerHistoryRemove(Fl_Widget*, std::string s, void* data);
  static std::string serverHistoryNormalize(const std::string s);

protected:
  Fl_Input *filter;
  ConnectionTree *tree;
  Fl_Box *statusBox;
  Fl_Button *editButton;
  Fl_Button *deleteButton;
  Fl_Button *favoriteButton;
  Fl_Suggestion_Input *serverName;
  std::list<std::string> serverHistory;
  std::string usedDir;

  ConnectionBook book;
  // Result of a Tailscale scan running on a background thread
  struct TailscaleScan {
    std::atomic<bool> done{false};
    std::vector<TailscalePeer> peers;
    std::string error;
  };

  std::shared_ptr<TailscaleScan> scan;
  std::vector<TailscalePeer> peers;
  std::string tailscaleStatus;

  std::vector<ItemRef> itemRefs;
  std::string selectedEntryId;

  // Options in effect before a saved connection's options were loaded
  ViewerParameterSnapshot baseOptions;
  std::string loadedOptionsId;
};

#endif
