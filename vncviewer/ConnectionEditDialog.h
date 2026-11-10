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

#ifndef __CONNECTIONEDITDIALOG_H__
#define __CONNECTIONEDITDIALOG_H__

#include <string>
#include <vector>

#include <FL/Fl_Window.H>

struct ConnectionEntry;

class Fl_Box;
class Fl_Button;
class Fl_Check_Button;
class Fl_Input;
class Fl_Input_Choice;

class ConnectionEditDialog : public Fl_Window {
protected:
  ConnectionEditDialog(const char* title,
                       const std::vector<std::string>& groups);

public:
  // Shows the dialog modally. Returns true and updates the entry if
  // the user pressed OK.
  static bool run(const char* title, ConnectionEntry* entry,
                  const std::vector<std::string>& groups);

private:
  void updateLoginStatus();

  static void handleOk(Fl_Widget* widget, void* data);
  static void handleCancel(Fl_Widget* widget, void* data);
  static void handleForget(Fl_Widget* widget, void* data);

private:
  Fl_Input* name;
  Fl_Input* host;
  Fl_Input* username;
  Fl_Input_Choice* group;
  Fl_Check_Button* favorite;
  Fl_Box* loginStatus;
  Fl_Button* forgetButton;

  bool accepted;
};

#endif
