# Saved connections, logins and Tailscale (dj-pearson fork)

This fork extends the TigerVNC viewer's connection window. None of this
is part of upstream TigerVNC.

## The connection window

The window lists computers in sections:

- **Favorites**: saved connections marked as favorite.
- **Groups**: saved connections, one section per group, then
  "Saved connections" for those without a group.
- **Recent**: servers from the history that are not saved.
- **Tailscale**: computers online on your tailnet. Those answering on a
  VNC port (5900 to 5902) come first and show the port.

Type in **Search** to filter all sections. Click a row to put its
address in the "VNC server" field, double-click it (or press Enter) to
connect.

Buttons on the right:

- **Add...** saves the selected or typed server. A Tailscale computer
  gets its host name and the "Tailscale" group filled in.
- **Edit...** changes name, address, username, group and favorite, and
  can forget a saved login.
- **Delete** removes the saved connection, its options and its login.
- **Favorite / Unfavorite** toggles the Favorites section.
- **Refresh** asks Tailscale again.

## Options per connection

Each saved connection keeps its own viewer options. Selecting it loads
them; **Options...** then edits that connection's options, and they are
written back when you connect. Selecting anything else switches back to
the general options. Typing the address of a saved connection by hand
also uses that connection's options.

## Saved logins

The login prompt has **Remember login on this computer**. When ticked,
the username and password are stored in Windows Credential Manager
(as `TigerVNC:<host>:<port>`, visible under *Control Panel > Credential
Manager > Windows Credentials*) once the server has accepted them.
Nothing secret is written to TigerVNC's own files.

The next connection to that server logs in without asking. If the server
rejects the saved login, the prompt comes back with the box ticked, and
the new login replaces the old one. Untick the box to forget it.

`VNC_USERNAME`/`VNC_PASSWORD` and `-PasswordFile` still take precedence.

Linux and macOS don't have a credential store backend yet. There the box
is hidden and logins are not stored.

## Command line

    vncviewer "Office PC"

connects to the saved connection with that name (case-insensitive),
using its options. Plain host names keep working as before.

## Files

In the TigerVNC config directory (`%APPDATA%\TigerVNC` on Windows,
`~/.config/tigervnc` elsewhere):

- `connections.conf`: the saved connections (name, host, username,
  group, favorite, usage statistics).
- `connections/<id>.tigervnc`: each connection's options, in the normal
  `.tigervnc` format.
