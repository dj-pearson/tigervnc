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

#ifndef __RECONNECTDIALOG_H__
#define __RECONNECTDIALOG_H__

#include <string>

#include <FL/Fl_Window.H>

class Fl_Box;

// Counts down to an automatic reconnect, which the user can trigger
// early or cancel
class ReconnectDialog : public Fl_Window {
protected:
  ReconnectDialog(const std::string& server, const std::string& error);
  ~ReconnectDialog();

public:
  // Returns true to reconnect, false if the user cancelled
  static bool run(const std::string& server, const std::string& error,
                  int attempt, int delaySeconds);

  // Seconds to wait before the given attempt (1, 2, 4 ... 30)
  static int backoff(int attempt);

private:
  void updateCountdown();

  static void handleTick(void* data);
  static void handleNow(Fl_Widget* widget, void* data);
  static void handleCancel(Fl_Widget* widget, void* data);

private:
  Fl_Box* countdown;
  std::string countdownText;
  int attempt;
  int remaining;
  bool reconnect;
};

#endif
