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
#endif

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_ask.H>

#include <core/i18n.h>

#include "fltk/layout.h"
#include "ConnectionBook.h"
#include "ConnectionEditDialog.h"
#include "CredentialStore.h"

ConnectionEditDialog::ConnectionEditDialog(
  const char* title, const std::vector<std::string>& groups)
  : Fl_Window(420, 0, title), accepted(false)
{
  int x, y, width;
  Fl_Button* button;

  x = OUTER_MARGIN;
  y = OUTER_MARGIN;
  width = w() - OUTER_MARGIN * 2;

  y += INPUT_LABEL_OFFSET;
  name = new Fl_Input(x, y, width, INPUT_HEIGHT, _("Name:"));
  name->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);
  y += INPUT_HEIGHT + INNER_MARGIN;

  y += INPUT_LABEL_OFFSET;
  host = new Fl_Input(x, y, width, INPUT_HEIGHT,
                      _("VNC server (host, host:display or host::port):"));
  host->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);
  y += INPUT_HEIGHT + INNER_MARGIN;

  y += INPUT_LABEL_OFFSET;
  username = new Fl_Input(x, y, width, INPUT_HEIGHT,
                          _("Username (if the server asks for one):"));
  username->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);
  y += INPUT_HEIGHT + INNER_MARGIN;

  y += INPUT_LABEL_OFFSET;
  group = new Fl_Input_Choice(x, y, width, INPUT_HEIGHT, _("Group:"));
  group->align(FL_ALIGN_LEFT | FL_ALIGN_TOP);
  for (const std::string& g : groups)
    group->add(g.c_str());
  y += INPUT_HEIGHT + INNER_MARGIN;

  favorite = new Fl_Check_Button(LBLRIGHT(x, y, CHECK_MIN_WIDTH,
                                          CHECK_HEIGHT,
                                          _("Show in favorites")));
  y += CHECK_HEIGHT + INNER_MARGIN;

  loginStatus = new Fl_Box(x, y, width - BUTTON_WIDTH - INNER_MARGIN,
                           BUTTON_HEIGHT);
  loginStatus->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  forgetButton = new Fl_Button(x + width - BUTTON_WIDTH, y,
                               BUTTON_WIDTH, BUTTON_HEIGHT,
                               _("Forget login"));
  forgetButton->callback(handleForget, this);
  forgetButton->tooltip(_("Logins are saved by ticking \"Remember login\" "
                          "when the server asks for a password"));
  y += BUTTON_HEIGHT + INNER_MARGIN;

  y += OUTER_MARGIN - INNER_MARGIN;

  x = w() - OUTER_MARGIN - BUTTON_WIDTH;
  button = new Fl_Return_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, fl_ok);
  button->callback(handleOk, this);

  x -= BUTTON_WIDTH + INNER_MARGIN;
  button = new Fl_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, fl_cancel);
  button->callback(handleCancel, this);
  button->shortcut(FL_Escape);

  y += BUTTON_HEIGHT + OUTER_MARGIN;

  end();
  size(w(), y);
  callback(handleCancel, this);
  set_modal();
}

bool ConnectionEditDialog::run(const char* title, ConnectionEntry* entry,
                               const std::vector<std::string>& groups)
{
  ConnectionEditDialog dialog(title, groups);

  dialog.name->value(entry->name.c_str());
  dialog.host->value(entry->host.c_str());
  dialog.username->value(entry->username.c_str());
  dialog.group->value(entry->group.c_str());
  dialog.favorite->value(entry->favorite);
  dialog.updateLoginStatus();

  if (entry->name.empty() && !entry->host.empty())
    dialog.name->take_focus();

  dialog.show();
  while (dialog.shown())
    Fl::wait();

  if (!dialog.accepted)
    return false;

  entry->name = dialog.name->value();
  entry->host = dialog.host->value();
  entry->username = dialog.username->value();
  entry->group = dialog.group->value() ? dialog.group->value() : "";
  entry->favorite = dialog.favorite->value();
  if (entry->name.empty())
    entry->name = entry->host;

  return true;
}

void ConnectionEditDialog::updateLoginStatus()
{
  std::string key;

  if (!CredentialStore::available()) {
    loginStatus->label(_("Saved logins not supported here"));
    forgetButton->deactivate();
    return;
  }

  key = ConnectionBook::credentialKey(host->value());
  if (host->value()[0] != '\0' && CredentialStore::exists(key)) {
    loginStatus->label(_("A login is saved for this server"));
    forgetButton->activate();
  } else {
    loginStatus->label(_("No saved login"));
    forgetButton->deactivate();
  }
}

void ConnectionEditDialog::handleOk(Fl_Widget* /*widget*/, void* data)
{
  ConnectionEditDialog* dialog = (ConnectionEditDialog*)data;

  if (dialog->host->value()[0] == '\0') {
    fl_alert(_("Please enter the address of the VNC server."));
    dialog->host->take_focus();
    return;
  }

  dialog->accepted = true;
  dialog->hide();
}

void ConnectionEditDialog::handleCancel(Fl_Widget* /*widget*/, void* data)
{
  ConnectionEditDialog* dialog = (ConnectionEditDialog*)data;

  dialog->accepted = false;
  dialog->hide();
}

void ConnectionEditDialog::handleForget(Fl_Widget* /*widget*/, void* data)
{
  ConnectionEditDialog* dialog = (ConnectionEditDialog*)data;

  CredentialStore::forget(
    ConnectionBook::credentialKey(dialog->host->value()));
  dialog->updateLoginStatus();
}
