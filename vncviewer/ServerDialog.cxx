/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2012 Samuel Mannehed <samuel@cendio.se> for Cendio AB
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
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <algorithm>
#include <initializer_list>
#include <thread>
#include <libgen.h>

// FIXME: Workaround for FLTK including windows.h
#ifdef WIN32
#include <winsock2.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Tree.H>
#include <FL/Fl_Tree_Item.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>

#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>
#include <core/xdgdirs.h>

#include <network/TcpSocket.h>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "fltk/Fl_Suggestion_Input.h"
#include "ConnectionEditDialog.h"
#include "CredentialStore.h"
#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "vncviewer.h"
#include "parameters.h"

static core::LogWriter vlog("ServerDialog");

const char* SERVER_HISTORY="tigervnc.history";

// VNC ports we look for on Tailscale computers (displays :0 to :2)
static const std::vector<int> TAILSCALE_VNC_PORTS = { 5900, 5901, 5902 };
static const int TAILSCALE_PROBE_TIMEOUT = 1500;

// Fl_Tree that reports double clicks and Enter on a row
class ConnectionTree : public Fl_Tree {
public:
  ConnectionTree(int x, int y, int w, int h)
    : Fl_Tree(x, y, w, h), activateCb(nullptr), activateData(nullptr) {}

  void on_activate(void (*cb)(void*), void* data) {
    activateCb = cb;
    activateData = data;
  }

  int handle(int event) override {
    int ret;

    if ((event == FL_KEYBOARD) &&
        ((Fl::event_key() == FL_Enter) ||
         (Fl::event_key() == FL_KP_Enter)) &&
        (first_selected_item() != nullptr)) {
      if (activateCb)
        activateCb(activateData);
      return 1;
    }

    ret = Fl_Tree::handle(event);

    if ((event == FL_PUSH) && (Fl::event_button() == FL_LEFT_MOUSE) &&
        Fl::event_clicks() && (find_clicked() != nullptr) &&
        (find_clicked()->children() == 0)) {
      Fl::event_clicks(0);
      if (activateCb)
        activateCb(activateData);
      return 1;
    }

    return ret;
  }

private:
  void (*activateCb)(void*);
  void* activateData;
};

static std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return tolower(c); });
  return s;
}

static bool matchesFilter(const std::string& filter,
                          std::initializer_list<std::string> fields)
{
  if (filter.empty())
    return true;
  for (const std::string& field : fields) {
    if (toLower(field).find(filter) != std::string::npos)
      return true;
  }
  return false;
}

// Host part of a server string, lower case
static std::string hostOf(const std::string& server)
{
  std::string host;
  int port;

  try {
    network::getHostAndPort(server.c_str(), &host, &port);
  } catch (std::exception&) {
    return toLower(server);
  }

  return toLower(host);
}

static bool fileExists(const std::string& path)
{
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

ServerDialog::ServerDialog()
  : Fl_Window(640, 0, "TigerVNC")
{
  int x, y, x2, treeW, treeH;
  Fl_Button *button;
  Fl_Box *divider;

  x = OUTER_MARGIN;
  y = OUTER_MARGIN;

  treeW = w() - OUTER_MARGIN*2 - BUTTON_WIDTH - INNER_MARGIN;
  treeH = 300;

  filter = new Fl_Input(LBLLEFT(x, y, treeW, INPUT_HEIGHT, _("Search:")));
  filter->when(FL_WHEN_CHANGED);
  filter->callback(handleFilter, this);

  y += INPUT_HEIGHT + INNER_MARGIN;

  tree = new ConnectionTree(x, y, treeW, treeH);
  tree->showroot(0);
  tree->selectmode(FL_TREE_SELECT_SINGLE);
  tree->callback(handleTree, this);
  tree->on_activate(handleTreeActivate, this);

  // Buttons managing the saved connections
  x2 = x + treeW + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Add..."));
  button->callback(handleAdd, this);
  button->tooltip(_("Save the selected or entered server as a "
                    "connection, together with the current options"));

  editButton = new Fl_Button(x2, y + (BUTTON_HEIGHT + INNER_MARGIN),
                             BUTTON_WIDTH, BUTTON_HEIGHT, _("Edit..."));
  editButton->callback(handleEdit, this);

  deleteButton = new Fl_Button(x2, y + (BUTTON_HEIGHT + INNER_MARGIN)*2,
                               BUTTON_WIDTH, BUTTON_HEIGHT, _("Delete"));
  deleteButton->callback(handleDelete, this);

  favoriteButton = new Fl_Button(x2, y + (BUTTON_HEIGHT + INNER_MARGIN)*3,
                                 BUTTON_WIDTH, BUTTON_HEIGHT,
                                 _("Favorite"));
  favoriteButton->callback(handleFavorite, this);

  button = new Fl_Button(x2, y + treeH - BUTTON_HEIGHT,
                         BUTTON_WIDTH, BUTTON_HEIGHT, _("Refresh"));
  button->callback(handleRefresh, this);
  button->tooltip(_("Look for computers on your Tailscale network again"));

  y += treeH + TIGHT_MARGIN;

  statusBox = new Fl_Box(x, y, treeW, INPUT_HEIGHT - 5);
  statusBox->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  statusBox->labelsize(FL_NORMAL_SIZE - 1);

  y += statusBox->h() + INNER_MARGIN;

  serverName = new Fl_Suggestion_Input(
    LBLLEFT(x, y, w() - OUTER_MARGIN*2, INPUT_HEIGHT, _("VNC server:")), {}
  );
  serverName->call_on_remove(onServerHistoryRemove, this);
  serverName->call_to_normalize(serverHistoryNormalize);

  y += INPUT_HEIGHT + INNER_MARGIN;

  x2 = x;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Options..."));
  button->callback(this->handleOptions, this);
  button->tooltip(_("Options are remembered per saved connection"));
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Load..."));
  button->callback(this->handleLoad, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Save as..."));
  button->callback(this->handleSaveAs, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  y += BUTTON_HEIGHT + INNER_MARGIN;

  divider = new Fl_Box(0, y, w(), 2);
  divider->box(FL_THIN_DOWN_FRAME);

  y += divider->h() + INNER_MARGIN;

  // Symmetric margin around bottom button bar
  y += OUTER_MARGIN - INNER_MARGIN;

  button = new Fl_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("About..."));
  button->callback(this->handleAbout, this);

  x2 = w() - OUTER_MARGIN - BUTTON_WIDTH*2 - INNER_MARGIN*1;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  button->callback(this->handleCancel, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Return_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Connect"));
  button->callback(this->handleConnect, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  y += BUTTON_HEIGHT + INNER_MARGIN;

  /* Needed for resize to work sanely */
  resizable(nullptr);
  h(y-INNER_MARGIN+OUTER_MARGIN);

  callback(this->handleCancel, this);

  baseOptions = snapshotViewerParameters();
}


ServerDialog::~ServerDialog()
{
  // A running scan finishes on its own and frees its result
  Fl::remove_timeout(checkTailscaleScan, this);
}


std::string ServerDialog::run(const char* servername)
{
  ServerDialog dialog;

  dialog.serverName->value(servername);

  dialog.show();

  try {
    dialog.loadServerHistory();
    dialog.serverName->set_suggestions(dialog.serverHistory);
  } catch (std::exception& e) {
    vlog.error(_("Unable to load the server history: %s"), e.what());
  }

  // The most recent connection is the best guess for the next one
  if (!dialog.serverHistory.empty())
    dialog.serverName->value(dialog.serverHistory.front().c_str());

  dialog.loadBook();
  dialog.rebuildTree();

  // Preselect the matching row so its options are in effect
  if (dialog.serverName->value() && dialog.serverName->value()[0]) {
    ConnectionEntry* entry;
    ItemRef ref;

    entry = dialog.book.findByHost(dialog.serverName->value());
    if (entry) {
      ref.kind = ItemRef::Entry;
      ref.id = entry->id;
    } else {
      ref.kind = ItemRef::Recent;
      ref.host = dialog.serverName->value();
    }
    dialog.selectRef(ref);
  }

  dialog.updateButtons();

  dialog.startTailscaleScan();

  while (dialog.shown()) Fl::wait();

  if (dialog.serverName->value() == nullptr) {
    return "";
  }

  return dialog.serverName->value();
}

void ServerDialog::loadBook()
{
  try {
    book.load();
  } catch (std::exception& e) {
    vlog.error(_("Unable to load the saved connections: %s"), e.what());
  }
}

void ServerDialog::saveBook()
{
  try {
    book.save();
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the connections: %s"), e.what());
    fl_alert(_("Unable to save the connections:\n\n%s"), e.what());
  }
}

void ServerDialog::addRow(Fl_Tree_Item* parent, const std::string& label,
                          const ItemRef& ref)
{
  Fl_Tree_Item* item;

  item = tree->add(parent, label.c_str());
  if (item == nullptr)
    return;

  itemRefs.push_back(ref);
  item->user_data((void*)(intptr_t)itemRefs.size());
}

void ServerDialog::rebuildTree()
{
  std::string needle;
  std::vector<const ConnectionEntry*> sorted;
  const ItemRef* current;
  ItemRef previous;
  bool hadSelection;

  current = selectedRef();
  hadSelection = current != nullptr;
  if (hadSelection)
    previous = *current;

  needle = toLower(filter->value());

  tree->clear_children(tree->root());
  itemRefs.clear();

  for (const ConnectionEntry& e : book.entries())
    sorted.push_back(&e);
  std::sort(sorted.begin(), sorted.end(),
            [](const ConnectionEntry* a, const ConnectionEntry* b) {
              return toLower(a->name) < toLower(b->name);
            });

  auto entryLabel = [](const ConnectionEntry* e) {
    if (e->name == e->host)
      return e->name;
    return core::format("%s  (%s)", e->name.c_str(), e->host.c_str());
  };

  auto entryMatches = [&needle](const ConnectionEntry* e) {
    return matchesFilter(needle, {e->name, e->host, e->username,
                                  e->group});
  };

  auto addSection = [this](const char* title) {
    Fl_Tree_Item* item;
    item = tree->add(tree->root(), title);
    item->labelfont(FL_HELVETICA_BOLD);
    itemRefs.push_back({ItemRef::Section, "", "", ""});
    item->user_data((void*)(intptr_t)itemRefs.size());
    return item;
  };

  // Favorites
  Fl_Tree_Item* section = nullptr;
  for (const ConnectionEntry* e : sorted) {
    if (!e->favorite || !entryMatches(e))
      continue;
    if (section == nullptr)
      section = addSection(_("Favorites"));
    addRow(section, entryLabel(e), {ItemRef::Entry, e->id, e->host, ""});
  }

  // Groups, then everything without a group
  std::vector<std::string> groups = book.groups();
  groups.push_back("");
  for (const std::string& group : groups) {
    section = nullptr;
    for (const ConnectionEntry* e : sorted) {
      if (e->group != group || !entryMatches(e))
        continue;
      if (section == nullptr) {
        if (group.empty())
          section = addSection(_("Saved connections"));
        else
          section = addSection(group.c_str());
      }
      addRow(section, entryLabel(e), {ItemRef::Entry, e->id, e->host, ""});
    }
  }

  // History that isn't already saved
  section = nullptr;
  for (const std::string& host : serverHistory) {
    std::string label, name, h;

    if (book.findByHost(host) != nullptr)
      continue;

    // Tailscale addresses are easier to recognise by name
    label = host;
    h = hostOf(host);
    for (const TailscalePeer& p : peers) {
      if (h == p.ipv4 || h == toLower(p.dnsName) ||
          h == toLower(p.hostName)) {
        label = core::format("%s  (%s)", host.c_str(), p.hostName.c_str());
        name = p.hostName;
        break;
      }
    }

    if (!matchesFilter(needle, {host, name}))
      continue;
    if (section == nullptr)
      section = addSection(_("Recent"));

    addRow(section, label, {ItemRef::Recent, "", host, name});
  }

  // Tailscale, those with VNC first
  std::vector<const TailscalePeer*> online;
  for (const TailscalePeer& p : peers) {
    if (!p.online && !p.self)
      continue;
    if (!matchesFilter(needle, {p.hostName, p.dnsName, p.ipv4, p.os}))
      continue;
    online.push_back(&p);
  }
  std::stable_sort(online.begin(), online.end(),
                   [](const TailscalePeer* a, const TailscalePeer* b) {
                     return (a->vncPort != 0) && (b->vncPort == 0);
                   });

  section = nullptr;
  for (const TailscalePeer* p : online) {
    std::string label;

    if (section == nullptr)
      section = addSection(_("Discovered on Tailscale"));

    label = core::format("%s  (%s", p->hostName.c_str(), p->ipv4.c_str());
    if (!p->os.empty())
      label += ", " + p->os;
    label += ")";
    if (p->self)
      label += _("  - this computer");
    if (p->vncPort != 0)
      label += core::format(_("  - VNC on port %d"), p->vncPort);
    if (book.findByHost(p->vncAddress()) != nullptr)
      label += _("  - saved");

    addRow(section, label,
           {ItemRef::Tailscale, "", p->vncAddress(), p->hostName});
  }

  if (hadSelection && previous.kind != ItemRef::Section) {
    for (Fl_Tree_Item* item = tree->first(); item; item = tree->next(item)) {
      intptr_t idx = (intptr_t)item->user_data();
      if (idx <= 0 || (size_t)idx > itemRefs.size())
        continue;
      const ItemRef& ref = itemRefs[idx - 1];
      if (ref.kind != previous.kind)
        continue;
      if ((ref.kind == ItemRef::Entry) ? (ref.id == previous.id)
                                       : (ref.host == previous.host)) {
        tree->select_only(item, 0);
        tree->show_item(item);
        break;
      }
    }
  }

  tree->redraw();
  updateButtons();
}

const ServerDialog::ItemRef* ServerDialog::selectedRef()
{
  Fl_Tree_Item* item;
  intptr_t idx;

  item = tree->first_selected_item();
  if (item == nullptr)
    return nullptr;

  idx = (intptr_t)item->user_data();
  if (idx <= 0 || (size_t)idx > itemRefs.size())
    return nullptr;

  return &itemRefs[idx - 1];
}

void ServerDialog::selectRef(const ItemRef& want)
{
  for (Fl_Tree_Item* item = tree->first(); item; item = tree->next(item)) {
    intptr_t idx = (intptr_t)item->user_data();
    if (idx <= 0 || (size_t)idx > itemRefs.size())
      continue;
    const ItemRef& ref = itemRefs[idx - 1];
    if (ref.kind != want.kind)
      continue;
    if ((ref.kind == ItemRef::Entry) ? (ref.id != want.id)
                                     : !sameServer(ref.host, want.host))
      continue;

    tree->select_only(item, 0);
    tree->show_item(item);
    if (ref.kind == ItemRef::Entry) {
      selectedEntryId = ref.id;
      applyEntryOptions(ref.id);
    } else {
      selectedEntryId.clear();
      applyEntryOptions("");
    }
    break;
  }

  updateButtons();
}

void ServerDialog::updateButtons()
{
  const ItemRef* ref;
  ConnectionEntry* entry;

  ref = selectedRef();
  entry = nullptr;
  if (ref && ref->kind == ItemRef::Entry)
    entry = book.findById(ref->id);

  if (entry) {
    editButton->activate();
    deleteButton->activate();
    favoriteButton->activate();
    favoriteButton->label(entry->favorite ? _("Unfavorite") : _("Favorite"));
  } else {
    editButton->deactivate();
    deleteButton->deactivate();
    favoriteButton->deactivate();
    favoriteButton->label(_("Favorite"));
  }
}

// Switches the global viewer options to those of a saved connection,
// or back to the general ones for an empty id
void ServerDialog::applyEntryOptions(const std::string& id)
{
  if (id == loadedOptionsId)
    return;

  // Keep any changes made to the general options
  if (loadedOptionsId.empty())
    baseOptions = snapshotViewerParameters();

  if (id.empty()) {
    restoreViewerParameters(baseOptions);
  } else {
    std::string path;

    try {
      path = ConnectionBook::optionsPath(id);
      if (fileExists(path)) {
        resetViewerParameters();
        loadViewerParameters(path.c_str());
      } else {
        restoreViewerParameters(baseOptions);
      }
    } catch (std::exception& e) {
      vlog.error(_("Unable to load the options for this connection: %s"),
                 e.what());
      restoreViewerParameters(baseOptions);
    }
  }

  loadedOptionsId = id;
}

void ServerDialog::startTailscaleScan()
{
  std::shared_ptr<TailscaleScan> result;

  // Already looking?
  if (scan && !scan->done)
    return;

  result = std::make_shared<TailscaleScan>();
  scan = result;

  statusBox->label(_("Looking for computers on Tailscale..."));
  statusBox->redraw();

  // The tailscale CLI and the port probes take a second or two, so
  // keep the window responsive
  std::thread([result]() {
    try {
      result->peers = TailscaleDiscovery::listPeers();
      TailscaleDiscovery::probeVncPorts(&result->peers,
                                        TAILSCALE_VNC_PORTS,
                                        TAILSCALE_PROBE_TIMEOUT);
    } catch (std::exception& e) {
      result->peers.clear();
      result->error = e.what();
    }
    result->done = true;
  }).detach();

  Fl::add_timeout(0.1, checkTailscaleScan, this);
}

void ServerDialog::checkTailscaleScan(void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  int online, withVnc;

  if (!dialog->scan->done) {
    Fl::repeat_timeout(0.1, checkTailscaleScan, data);
    return;
  }

  if (!dialog->scan->error.empty()) {
    vlog.info("%s", dialog->scan->error.c_str());
    dialog->peers.clear();
    dialog->tailscaleStatus = dialog->scan->error;
  } else {
    dialog->peers = dialog->scan->peers;

    online = withVnc = 0;
    for (const TailscalePeer& p : dialog->peers) {
      if (p.online || p.self)
        online++;
      if (p.vncPort != 0)
        withVnc++;
    }

    dialog->tailscaleStatus = core::format(_("Tailscale: %d computers "
                                             "online, %d with VNC"),
                                           online, withVnc);
  }

  dialog->statusBox->copy_label(dialog->tailscaleStatus.c_str());
  dialog->statusBox->redraw();

  dialog->rebuildTree();
}

void ServerDialog::handleFilter(Fl_Widget* /*widget*/, void *data)
{
  ((ServerDialog*)data)->rebuildTree();
}

void ServerDialog::handleTree(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const ItemRef* ref;

  if (dialog->tree->callback_reason() != FL_TREE_REASON_SELECTED)
    return;

  ref = dialog->selectedRef();
  if (ref == nullptr || ref->kind == ItemRef::Section) {
    dialog->updateButtons();
    return;
  }

  dialog->serverName->value(ref->host.c_str());

  if (ref->kind == ItemRef::Entry) {
    dialog->selectedEntryId = ref->id;
    dialog->applyEntryOptions(ref->id);
  } else {
    dialog->selectedEntryId.clear();
    dialog->applyEntryOptions("");
  }

  dialog->updateButtons();
}

void ServerDialog::handleTreeActivate(void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const ItemRef* ref;

  ref = dialog->selectedRef();
  if (ref == nullptr || ref->kind == ItemRef::Section)
    return;

  handleConnect(nullptr, dialog);
}

void ServerDialog::handleAdd(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const ItemRef* ref;
  ConnectionEntry entry;
  std::string id;

  ref = dialog->selectedRef();
  if (ref && ref->kind == ItemRef::Tailscale) {
    entry.name = ref->label;
    entry.host = ref->host;
    entry.group = "Tailscale";
  } else if (dialog->serverName->value()) {
    entry.host = dialog->serverName->value();
    if (ref && ref->kind == ItemRef::Recent && ref->host == entry.host)
      entry.name = ref->label;
  }

  if (!ConnectionEditDialog::run(_("Add connection"), &entry,
                                 dialog->book.groups()))
    return;

  id = dialog->book.add(entry).id;
  dialog->saveBook();

  // Start it off with the options currently in effect
  try {
    saveViewerParameters(ConnectionBook::optionsPath(id).c_str(),
                         entry.host.c_str());
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the options for this connection: %s"),
               e.what());
  }

  // Those options now belong to the new connection
  dialog->loadedOptionsId = id;
  dialog->selectedEntryId = id;

  dialog->serverName->value(entry.host.c_str());
  dialog->rebuildTree();
  dialog->selectRef({ItemRef::Entry, id, entry.host, ""});
}

void ServerDialog::handleEdit(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const ItemRef* ref;
  ConnectionEntry* entry;
  ConnectionEntry copy;

  ref = dialog->selectedRef();
  if (ref == nullptr || ref->kind != ItemRef::Entry)
    return;

  entry = dialog->book.findById(ref->id);
  if (entry == nullptr)
    return;

  copy = *entry;
  if (!ConnectionEditDialog::run(_("Edit connection"), &copy,
                                 dialog->book.groups()))
    return;

  *entry = copy;
  dialog->saveBook();

  dialog->serverName->value(copy.host.c_str());
  dialog->rebuildTree();
  dialog->selectRef({ItemRef::Entry, copy.id, copy.host, ""});
}

void ServerDialog::handleDelete(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const ItemRef* ref;
  ConnectionEntry* entry;
  std::string id, name, host, key;
  bool hasLogin;
  int choice;

  ref = dialog->selectedRef();
  if (ref == nullptr || ref->kind != ItemRef::Entry)
    return;

  entry = dialog->book.findById(ref->id);
  if (entry == nullptr)
    return;

  id = entry->id;
  name = entry->name;
  host = entry->host;
  key = ConnectionBook::credentialKey(host);
  hasLogin = CredentialStore::exists(key);

  if (hasLogin)
    choice = fl_choice(_("Delete the saved connection \"%s\"?\n\n"
                         "Its saved login will also be removed."),
                       nullptr, fl_cancel, _("Delete"), name.c_str());
  else
    choice = fl_choice(_("Delete the saved connection \"%s\"?"),
                       nullptr, fl_cancel, _("Delete"), name.c_str());
  if (choice != 2)
    return;

  dialog->book.remove(id);
  dialog->saveBook();

  try {
    ::remove(ConnectionBook::optionsPath(id).c_str());
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
  }

  if (hasLogin && dialog->book.findByHost(host) == nullptr)
    CredentialStore::forget(key);

  dialog->selectedEntryId.clear();
  dialog->applyEntryOptions("");
  dialog->rebuildTree();
}

void ServerDialog::handleFavorite(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const ItemRef* ref;
  ConnectionEntry* entry;
  std::string id;

  ref = dialog->selectedRef();
  if (ref == nullptr || ref->kind != ItemRef::Entry)
    return;

  entry = dialog->book.findById(ref->id);
  if (entry == nullptr)
    return;

  entry->favorite = !entry->favorite;
  id = entry->id;
  dialog->saveBook();
  dialog->rebuildTree();
  dialog->selectRef({ItemRef::Entry, id, "", ""});
}

void ServerDialog::handleRefresh(Fl_Widget* /*widget*/, void *data)
{
  ((ServerDialog*)data)->startTailscaleScan();
}

void ServerDialog::handleOptions(Fl_Widget* /*widget*/, void* /*data*/)
{
  OptionsDialog::showDialog();
}


void ServerDialog::handleLoad(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();

  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TigerVNC configuration (*.tigervnc)"),
                                                      Fl_File_Chooser::SINGLE,
                                                      _("Select a TigerVNC configuration file"));
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  // Block until user picks something.
  while(file_chooser->shown())
    Fl::wait();
  
  // Did the user hit cancel?
  if (file_chooser->value() == nullptr) {
    delete(file_chooser);
    return;
  }
  
  const char* filename = file_chooser->value();
  dialog->updateUsedDir(filename);

  try {
    dialog->serverName->value(loadViewerParameters(filename).c_str());

    // The loaded options are the general ones now
    dialog->tree->deselect_all(nullptr, 0);
    dialog->selectedEntryId.clear();
    dialog->loadedOptionsId.clear();
    dialog->baseOptions = snapshotViewerParameters();
    dialog->updateButtons();
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to load the specified configuration file:\n\n%s"),
             e.what());
  }

  delete(file_chooser);
}


void ServerDialog::handleSaveAs(Fl_Widget* /*widget*/, void* data)
{ 
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();
  const char* filename;
  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();
  
  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TigerVNC configuration (*.tigervnc)"),
                                                      Fl_File_Chooser::CREATE,
                                                      _("Save the TigerVNC configuration to file"));
  
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  while(1) {
    
    // Block until user picks something.
    while(file_chooser->shown())
      Fl::wait();
    
    // Did the user hit cancel?
    if (file_chooser->value() == nullptr) {
      delete(file_chooser);
      return;
    }
    
    filename = file_chooser->value();
    dialog->updateUsedDir(filename);
    
    FILE* f = fopen(filename, "r");
    if (f) {

      // The file already exists.
      fclose(f);
      int overwrite_choice = fl_choice(_("%s already exists. Do you want to overwrite?"), 
                                       nullptr, _("No"), _("Overwrite"), filename);
      if (overwrite_choice != 2) {

        // If the user doesn't want to overwrite:
        file_chooser->show();
        continue;
      }
    }

    break;
  }
  
  try {
    saveViewerParameters(filename, servername);
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to save the specified configuration "
               "file:\n\n%s"), e.what());
  }
  
  delete(file_chooser);
}


void ServerDialog::handleAbout(Fl_Widget* /*widget*/, void* /*data*/)
{
  about_vncviewer();
}


void ServerDialog::handleCancel(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  dialog->serverName->value("");
  dialog->hide();
}


void ServerDialog::handleConnect(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();
  ConnectionEntry* entry;

  if ((servername == nullptr) || (servername[0] == '\0')) {
    fl_alert(_("Please select a computer or enter the address of a "
               "VNC server."));
    return;
  }

  // Is this one of the saved connections?
  entry = nullptr;
  if (!dialog->selectedEntryId.empty()) {
    entry = dialog->book.findById(dialog->selectedEntryId);
    if (entry && !sameServer(entry->host, servername) &&
        (entry->host != servername))
      entry = nullptr;
  }
  if (entry == nullptr) {
    entry = dialog->book.findByHost(servername);
    if (entry)
      dialog->applyEntryOptions(entry->id);
  }

  dialog->hide();

  if (entry) {
    activeConnectionId = entry->id;

    try {
      saveViewerParameters(ConnectionBook::optionsPath(entry->id).c_str(),
                           servername);
    } catch (std::exception& e) {
      vlog.error(_("Unable to save the options for this connection: %s"),
                 e.what());
    }

    dialog->book.touch(entry->id, time(nullptr));
    dialog->saveBook();
  } else {
    activeConnectionId.clear();

    try {
      saveViewerParameters(nullptr, servername);
    } catch (std::exception& e) {
      vlog.error(_("Unable to save the default configuration: %s"),
                 e.what());
    }
  }

  // avoid duplicates in the history
  dialog->serverHistory.remove(servername);
  dialog->serverHistory.insert(dialog->serverHistory.begin(), servername);

  try {
    dialog->saveServerHistory();
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the server history: %s"), e.what());
  }
}


void ServerDialog::loadServerHistory()
{
  std::list<std::string> rawHistory;

  serverHistory.clear();

#ifdef _WIN32
  rawHistory = loadHistoryFromRegKey();
#else

  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, SERVER_HISTORY);

  /* Read server history from file */
  FILE* f = fopen(filepath, "r");
  if (!f) {
    if (errno == ENOENT) {
      // no history file
      return;
    }
    throw core::posix_error(
      core::format(_("Failed to open \"%s\""), filepath), errno);
  }

  int lineNr = 0;
  while (!feof(f)) {
    char line[256];

    // Read the next line
    lineNr++;
    if (!fgets(line, sizeof(line), f)) {
      if (feof(f))
        break;

      fclose(f);
      throw core::posix_error(
        core::format(_("Failed to read line %d in file \"%s\""),
                     lineNr, filepath),
        errno);
    }

    int len = strlen(line);

    if (len == (sizeof(line) - 1)) {
      fclose(f);
      std::string msg = core::format(_("Failed to read line %d in "
                                       "file \"%s\""),
                                     lineNr, filepath);
      throw std::runtime_error(
        core::format("%s: %s", msg.c_str(), _("Line too long")));
    }

    if ((len > 0) && (line[len-1] == '\n')) {
      line[len-1] = '\0';
      len--;
    }
    if ((len > 0) && (line[len-1] == '\r')) {
      line[len-1] = '\0';
      len--;
    }

    if (len == 0)
      continue;

    rawHistory.push_back(line);
  }

  fclose(f);
#endif

  // Filter out duplicates, even if they have different formats
  for (const std::string& entry : rawHistory) {
    if (std::find_if(serverHistory.begin(), serverHistory.end(),
                     [&entry](const std::string& s) {
                       return sameServer(s, entry);
                     }) != serverHistory.end())
      continue;
    serverHistory.push_back(entry);
  }
}

void ServerDialog::saveServerHistory()
{
#ifdef _WIN32
  saveHistoryToRegKey(serverHistory);
  return;
#endif

  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, SERVER_HISTORY);

  /* Write server history to file */
  FILE* f = fopen(filepath, "w+");
  if (!f) {
    std::string msg = core::format(_("Failed to open \"%s\""), filepath);
    throw core::posix_error(msg.c_str(), errno);
  }

  // Save the last X elements to the config file.
  size_t count = 0;
  for (const std::string& entry : serverHistory) {
    if (++count > SERVER_HISTORY_SIZE)
      break;
    fprintf(f, "%s\n", entry.c_str());
  }

  fclose(f);
}

void ServerDialog::updateUsedDir(const char* filename)
{
  char * name = strdup(filename);
  usedDir = dirname(name);
  free(name);
}

void ServerDialog::onServerHistoryRemove(Fl_Widget*, std::string s, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  dialog->serverHistory.remove(s);
  dialog->saveServerHistory();
  dialog->rebuildTree();
}

std::string ServerDialog::serverHistoryNormalize(const std::string s)
{
  // Convert to lowercase for case-insensitity
  std::string result = s;
  transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}
