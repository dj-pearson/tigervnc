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

#include <algorithm>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_draw.H>

#include <core/i18n.h>
#include <core/string.h>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "ReconnectDialog.h"

static const int MAX_BACKOFF = 30;

ReconnectDialog::ReconnectDialog(const std::string& server,
                                 const std::string& error)
  : Fl_Window(440, 0, _("Connection lost")),
    attempt(0), remaining(0), reconnect(false)
{
  int x, y, width, msgW, msgH;
  Fl_Box* box;
  Fl_Button* button;
  std::string message;

  x = OUTER_MARGIN;
  y = OUTER_MARGIN;
  width = w() - OUTER_MARGIN * 2;

  message = core::format(_("The connection to %s was lost:"),
                         server.c_str());
  box = new Fl_Box(x, y, width, INPUT_HEIGHT);
  box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
  box->labelfont(FL_HELVETICA_BOLD);
  box->copy_label(fltk_escape(message).c_str());
  y += INPUT_HEIGHT;

  // Server errors can be long, so let them wrap
  message = fltk_escape(error);
  msgW = width;
  msgH = 0;
  fl_font(FL_HELVETICA, FL_NORMAL_SIZE);
  fl_measure(message.c_str(), msgW, msgH, 0);
  if (msgW > width) {
    msgW = width;
    msgH = 0;
    fl_measure(message.c_str(), msgW, msgH, 0);
  }
  msgH = std::min(msgH + TEXT_PADDING * 2, 120);
  box = new Fl_Box(x, y, width, msgH);
  box->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE |
             FL_ALIGN_WRAP | FL_ALIGN_CLIP);
  box->copy_label(message.c_str());
  y += msgH + INNER_MARGIN;

  countdown = new Fl_Box(x, y, width, INPUT_HEIGHT);
  countdown->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += INPUT_HEIGHT + INNER_MARGIN;

  y += OUTER_MARGIN - INNER_MARGIN;

  x = w() - OUTER_MARGIN - BUTTON_WIDTH;
  button = new Fl_Return_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT,
                                _("Reconnect now"));
  button->callback(handleNow, this);

  x -= BUTTON_WIDTH + INNER_MARGIN;
  button = new Fl_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  button->callback(handleCancel, this);
  button->shortcut(FL_Escape);

  y += BUTTON_HEIGHT + OUTER_MARGIN;

  end();
  size(w(), y);
  callback(handleCancel, this);
  set_modal();
}

ReconnectDialog::~ReconnectDialog()
{
  Fl::remove_timeout(handleTick, this);
}

int ReconnectDialog::backoff(int attempt)
{
  int delay;

  if (attempt < 1)
    attempt = 1;

  delay = 1;
  for (int i = 1; i < attempt && delay < MAX_BACKOFF; i++)
    delay *= 2;

  return std::min(delay, MAX_BACKOFF);
}

bool ReconnectDialog::run(const std::string& server,
                          const std::string& error,
                          int attempt, int delaySeconds)
{
  ReconnectDialog dialog(server, error);

  dialog.attempt = attempt;
  dialog.remaining = delaySeconds;
  dialog.updateCountdown();

  dialog.show();
  Fl::add_timeout(1.0, handleTick, &dialog);

  while (dialog.shown())
    Fl::wait();

  return dialog.reconnect;
}

void ReconnectDialog::updateCountdown()
{
  if (remaining > 0)
    countdownText = core::format(_("Reconnecting in %d s (attempt %d)..."),
                                 remaining, attempt);
  else
    countdownText = core::format(_("Reconnecting (attempt %d)..."),
                                 attempt);
  countdown->copy_label(countdownText.c_str());
  countdown->redraw();
}

void ReconnectDialog::handleTick(void* data)
{
  ReconnectDialog* dialog = (ReconnectDialog*)data;

  dialog->remaining--;
  dialog->updateCountdown();

  if (dialog->remaining <= 0) {
    dialog->reconnect = true;
    dialog->hide();
    return;
  }

  Fl::repeat_timeout(1.0, handleTick, data);
}

void ReconnectDialog::handleNow(Fl_Widget* /*widget*/, void* data)
{
  ReconnectDialog* dialog = (ReconnectDialog*)data;

  dialog->reconnect = true;
  dialog->hide();
}

void ReconnectDialog::handleCancel(Fl_Widget* /*widget*/, void* data)
{
  ReconnectDialog* dialog = (ReconnectDialog*)data;

  dialog->reconnect = false;
  dialog->hide();
}
