#!/usr/bin/env python3
import sys
import time
import rumps
import gopher_client
import subprocess
import threading
from typing import Optional
import os
import socket
from dataclasses import dataclass
from typing import List


def get_local_ip():
    """find our ip addr by sending a not really packet"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80)),
        return s.getsockname()[0]
    finally:
        s.close()


def create_listening_socket():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    actual_port = sock.getsockname()[1]

    return sock, actual_port


def broadcast_loop(gopher_name, listening_port, interval=5, bcast_port=43753):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    local_ip = get_local_ip()
    message = f"name:{gopher_name};ip:{local_ip};port:{listening_port};".encode()

    bcast_addr = ("255.255.255.255", bcast_port)
    try:
      while True:
          try:
            sock.sendto(message, bcast_addr)
          except OSError as e:
              print("Broadcast failed:", e)
          time.sleep(interval)
    finally:
        sock.close()


@dataclass
class Gopher:
    name: str
    ip: str
    port: int

def query_daemon_for_gophers(daemon_port: int = 43823) -> List[Gopher]:
    """
    Connects to the local daemon on TCP port `daemon_port`,
    reads a comma-separated list of name,ip,port per line,
    and returns a list of Gopher instances.
    """
    result: List[Gopher] = []
    try:
        with socket.create_connection(("127.0.0.1", daemon_port)) as sock:
            # Read up to 2048 bytes (matches your C++ buffer size)
            data = sock.recv(2048)
            if not data:
                return result

            text = data.decode("utf-8", errors="ignore")
            for line in text.splitlines():
                parts = line.strip().split(",")
                if len(parts) != 3:
                    continue
                name, ip, port_str = parts
                try:
                    port = int(port_str)
                except ValueError:
                    continue
                result.append(Gopher(name=name, ip=ip, port=port))

    except (ConnectionRefusedError, OSError):
        # Could not connect or read; return empty list
        pass

    return result


class GopherStatusApp(rumps.App):
    """macOS status bar app for Gopher client using RUMPS"""

    def __init__(self, enable_dev_mode=False, override_name=None):
        super().__init__(name="Gopher", title="Gopher")
        self.video_process: Optional[subprocess.Popen] = None

        # Use override name if provided, otherwise use system username
        self.username = override_name if override_name else os.getlogin()
        self.dev_mode = enable_dev_mode

        # Initialize and start C++ client (first client created)
        self.client = gopher_client.GopherClient()

        # Enable dev mode if requested
        if self.dev_mode:
            self.client.enable_dev_mode(True)
            self.title = "Gopher DEV"  # Visual indicator of dev mode

        success = self.client.initialize(
            self.username, 0
        )  # 0 here allows for the system to pick its own port
        if not success:
            rumps.alert("Gopher Error", "Failed to initialize Gopher client")
            rumps.quit_application()
            return

        self.client.set_incoming_call_callback(self.on_incoming_call)

        # setup the port recv port we'll use for the entire app, and start broadcasting our existance
        socket, recv_port = create_listening_socket()
        # Start broadcasting thread
        broadcasting_thread = threading.Thread(
                target=broadcast_loop, args=(self.username, recv_port), daemon=True
            )
        broadcasting_thread.start()

        self.ip = get_local_ip()
        self.port = recv_port     

        # Build initial menu
        self.build_initial_menu()

        # Start a timer to refresh peers every 5 seconds
        self.timer = rumps.Timer(self.refresh_peers, 5)
        self.timer.start()

    def build_initial_menu(self):
        """Build the initial menu structure"""
        items = []

        # Status info
        status_item = rumps.MenuItem(f"User: {self.username}", callback=None)
        items.append(status_item)

        if self.dev_mode:
            dev_item = rumps.MenuItem("Dev Mode: ON", callback=None)
            items.append(dev_item)

        items.append(rumps.separator)

        # Placeholder for peers
        placeholder = rumps.MenuItem("Refreshing...", callback=None)
        items.append(placeholder)

        items.append(rumps.separator)

        # Control items
        end_call_item = rumps.MenuItem(
            "End Current Call", callback=self.end_current_call
        )
        items.append(end_call_item)

        items.append(rumps.separator)

        # Quit
        quit_item = rumps.MenuItem("Quit", callback=self.quit_app)
        items.append(quit_item)

        self.menu.clear()
        for item in items:
            self.menu.add(item)

    def refresh_peers(self, _):
        """Poll available gophers and update menu items"""
        try:
            peers = query_daemon_for_gophers()

            # Remove our own entry from the peers list
            # peers = [p for p in peers if (p.name != self.username and p.ip != self.ip)]
            peers = [p for p in peers if (p.name != self.username)]

            # Rebuild menu while preserving structure
            items = []

            # Status info
            status_item = rumps.MenuItem(f"User: {self.username}", callback=None)
            items.append(status_item)

            if self.dev_mode:
                dev_item = rumps.MenuItem("Dev Mode: ON", callback=None)
                items.append(dev_item)

            items.append(rumps.separator)

            # Peers
            if peers:
                for idx, p in enumerate(peers):
                    if "(self)" in p.name:
                        # Special styling for self-call in dev mode
                        label = f"📞 {p.name}"
                    else:
                        label = f"📞 {p.name}@{p.ip}:{p.port}"

                    item = rumps.MenuItem(
                        label, callback=lambda _, idx=idx: self.call_peer(idx)
                    )
                    items.append(item)
            else:
                no_peers_item = rumps.MenuItem("No peers found", callback=None)
                items.append(no_peers_item)

            items.append(rumps.separator)

            # Control items
            call_status = (
                "In Call"
                if self.video_process and self.video_process.poll() is None
                else "Not in Call"
            )
            status_item = rumps.MenuItem(call_status, callback=None)
            items.append(status_item)

            end_call_item = rumps.MenuItem(
                "End Current Call", callback=self.end_current_call
            )
            items.append(end_call_item)

            items.append(rumps.separator)

            # Quit
            quit_item = rumps.MenuItem("Quit", callback=self.quit_app)
            items.append(quit_item)

            # Update menu
            self.menu.clear()
            for item in items:
                self.menu.add(item)

        except Exception as e:
            print(f"Error refreshing peers: {e}")

    def call_peer(self, idx):
        """Initiate call to a peer by launching video app"""
        try:
            peers = query_daemon_for_gophers()
            # peers = [p for p in peers if (p.name != self.username and p.ip != self.ip)]
            peers = [p for p in peers if (p.name != self.username)]

            if 0 <= idx < len(peers):
                p = peers[idx]

                if self.video_process and self.video_process.poll() is None:
                    rumps.alert("Gopher", "Already in Call", "End current call first")
                    return

                # Launch the dedicated video app
                print(
                    f"""\033[91mDEBUG: Launching with following params:\033[0m)
              \033[91m  IP: {p.ip}\033[0m
              \033[91m  Port: {p.port}\033[0m
              \033[91m  Name: {p.name}\033[0m
              \033[91m  Recv Port: {self.port}\033[0m
              \033[91m  Username: {self.username}\033[0m
              \033[91m  Dev Mode: {self.dev_mode}\033[0m"""
                )

                # target_ip=args.target_ip,
                # target_port=args.target_port,
                # username=args.username,
                # recv_port=args.recv_port,

                self._launch_video_app(p.ip, p.port, p.name, self.port)

            else:
                rumps.notification("Gopher", "Error", "Peer not found")
        except Exception as e:
            rumps.notification("Gopher", "Error", f"Failed to start call: {e}")
            print("call_peer exception:", e)

    def end_current_call(self, _):
        """End the current call by terminating video app"""
        try:
            if self.video_process and self.video_process.poll() is None:
                print("Terminating video process...")

                # Send SIGTERM first for graceful shutdown
                self.video_process.terminate()

                # Wait a bit for graceful shutdown
                try:
                    self.video_process.wait(timeout=5)
                    print("Video process terminated gracefully")
                except subprocess.TimeoutExpired:
                    print("Video process didn't respond to SIGTERM, sending SIGKILL")
                    # Force kill if it doesn't respond
                    self.video_process.kill()
                    self.video_process.wait()
                    print("Video process killed")

                self.video_process = None
                rumps.notification("Gopher", "Call Ended", "Video app terminated")
            else:
                rumps.notification(
                    "Gopher", "No Active Call", "No video call in progress"
                )
        except Exception as e:
            print(f"Error ending call: {e}")
            rumps.notification("Gopher", "Error", f"Failed to end call: {e}")

    def on_incoming_call(self, name, ip, port):
        """Show alert for incoming call; return True to accept"""
        try:
            choice = rumps.alert(
                title="Incoming Gopher Call",
                message=f"{name}@{ip}:{port}",
                ok="Accept",
                cancel="Decline",
            )
            accepted = choice == rumps.OK

            if accepted:
                rumps.notification(
                    "Gopher", "Call Accepted", f"Accepting call from {name}"
                )
            else:
                rumps.notification(
                    "Gopher", "Call Declined", f"Declined call from {name}"
                )

            return accepted
        except Exception as e:
            print(f"Error in incoming call handler: {e}")
            return False  # Decline on error

    def _launch_video_app(self, ip: str, port: int, peer_name: str, recv_port: int):
        """Launch the dedicated video display app"""
        try:
            # Path to the video app script
            video_app_path = os.path.join(
                os.path.dirname(__file__), "gopher_video_app.py"
            )

            # Build command - pass the username that's being used (could be overridden)
            cmd = [
                sys.executable,  # Python interpreter
                video_app_path,
                ip,
                str(port),
                self.username,
                str(recv_port)
            ]

            if self.dev_mode:
                cmd.append("--dev")

            print(f"Launching video app with command: {' '.join(cmd)}")

            # Launch the video app
            self.video_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # Combine stderr with stdout
                bufsize=1,
                universal_newlines=True,
            )

            # Start monitoring thread
            monitor_thread = threading.Thread(
                target=self._monitor_video_process, args=(peer_name,), daemon=True
            )
            monitor_thread.start()

            if "(self)" in peer_name:
                rumps.notification(
                    "Gopher",
                    "Self-Call Started (Dev Mode)",
                    "Video app launched for testing",
                )
            else:
                rumps.notification(
                    "Gopher", "Call Started", f"Video app launched for {peer_name}"
                )

        except Exception as e:
            print(f"Error launching video app: {e}")
            rumps.notification("Gopher", "Error", f"Failed to launch video app: {e}")
            self.video_process = None

    def _monitor_video_process(self, peer_name: str):
        """Monitor the video process in background thread"""
        if not self.video_process:
            return

        try:
            # Read output from the process
            while self.video_process.poll() is None:
                line = self.video_process.stdout.readline()
                if line:
                    print(f"Video app: {line.strip()}")

            # Wait for process to complete
            returncode = self.video_process.wait()

            # Notify when call ends
            if returncode == 0:
                rumps.notification(
                    "Gopher", "Call Ended", f"Call with {peer_name} completed"
                )
                print(f"Video app exited normally (code {returncode})")
            else:
                rumps.notification(
                    "Gopher", "Call Error", f"Video app exited with code {returncode}"
                )
                print(f"Video app exited with error code {returncode}")

        except Exception as e:
            print(f"Video process monitoring error: {e}")
            rumps.notification("Gopher", "Error", f"Video process error: {e}")
        finally:
            self.video_process = None

    def quit_app(self, _):
        """Clean shutdown"""
        try:
            print("Shutting down Gopher status app...")
            self.timer.stop()

            # End any active video call
            if self.video_process and self.video_process.poll() is None:
                print("Terminating video process...")
                self.video_process.terminate()
                try:
                    self.video_process.wait(timeout=3)
                    print("Video process terminated")
                except subprocess.TimeoutExpired:
                    print("Force killing video process...")
                    self.video_process.kill()
                    self.video_process.wait()
                    print("Video process killed")

            # Stop broadcasting
            self.client.stop_broadcasting()
            print("Broadcasting stopped")

        except Exception as e:
            print(f"Error during shutdown: {e}")
        finally:
            rumps.quit_application()


if __name__ == "__main__":
    # Check for dev mode flag
    dev_mode = "--dev" in sys.argv or "-d" in sys.argv
    override_name = None

    # Parse override name flag
    if "-o" in sys.argv:
        try:
            override_idx = sys.argv.index("-o")
            if override_idx + 1 < len(sys.argv):
                override_name = sys.argv[override_idx + 1]
                print(f"Using override name: {override_name}")
        except (ValueError, IndexError):
            print("Error parsing override name argument")

    if dev_mode:
        print("Starting Gopher in development mode...")
        print("You will be able to call yourself for testing purposes.")

    app = GopherStatusApp(enable_dev_mode=dev_mode, override_name=override_name)
    app.run()
