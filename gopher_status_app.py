#!/usr/bin/env python3
import sys
import time
import rumps
import gopher_client
import os

class GopherStatusApp(rumps.App):
    """macOS status bar app for Gopher client using RUMPS"""
    def __init__(self):
        super().__init__(name="Gopher", title="Gopher")
        self.username = os.getlogin()

        # Initialize and start C++ client
        self.client = gopher_client.GopherClient()
        self.client.initialize(self.username, 0)
        self.client.set_incoming_call_callback(self.on_incoming_call)
        self.client.start_broadcasting()

        # Build initial menu: placeholder and Quit
        placeholder = rumps.MenuItem("Refreshing...", callback=None)
        quit_item = rumps.MenuItem("Quit", callback=self.quit_app)
        self.menu = [placeholder, rumps.separator, quit_item]

        # Start a timer to refresh peers every 5 seconds
        self.timer = rumps.Timer(self.refresh_peers, 5)
        self.timer.start()

    def refresh_peers(self, _):
        """Poll available gophers and update menu items"""
        peers = self.client.get_available_gophers()
        # Rebuild menu: peers above separator, Quit at bottom
        quit_item = rumps.MenuItem("Quit", callback=self.quit_app)
        separator = rumps.separator
        new_menu = []
        for idx, p in enumerate(peers):
            label = f"{p.name}@{p.ip}:{p.port}"
            # Use a lambda with default idx to capture correctly
            item = rumps.MenuItem(label, callback=lambda _, idx=idx: self.call_peer(idx))
            new_menu.append(item)
        new_menu.extend([separator, quit_item])
        self.menu.clear()
        for item in new_menu:
            self.menu.add(item)

    def call_peer(self, idx):
        peers = self.client.get_available_gophers()
        if 0 <= idx < len(peers):
            p = peers[idx]
            success = self.client.start_call(p.ip, p.port)
            title = "Call Started" if success else "Call Failed"
            rumps.notification("Gopher", title, f"{p.name}@{p.ip}:{p.port}")
        else:
            rumps.notification("Gopher", "Error", "Peer not found")

    def on_incoming_call(self, name, ip, port):
        """Show alert for incoming call; return True to accept"""
        choice = rumps.alert(
            title="Incoming Gopher Call",
            message=f"{name}@{ip}:{port}",
            ok="Accept",
            cancel="Decline"
        )
        return choice == rumps.OK

    def quit_app(self, _):
        self.timer.stop()
        self.client.stop_broadcasting()
        rumps.quit_application()

if __name__ == '__main__':
    GopherStatusApp().run()
