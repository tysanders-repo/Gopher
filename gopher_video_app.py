import sys
import time
import signal
import argparse
import threading
import gopher_client
import os
import cv2
from typing import Optional

##########################################################################################


class GopherVideoApp:
    """Standalone video display application for Gopher calls"""

    def __init__(
        self,
        target_ip: str,
        target_port: int,
        username: str,
        recv_port: int,
        dev_mode: bool = False,
    ):
        self.target_ip = target_ip
        self.target_port = target_port
        self.username = username
        self.recv_port = recv_port
        self.dev_mode = dev_mode
        self.client: Optional[gopher_client.GopherClient] = None
        self.running = True
        self.call_active = False
        self.video_display_thread = None

        # Set up signal handlers for clean shutdown
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

    def _signal_handler(self, signum, frame):
        """Handle shutdown signals"""
        print(f"Received signal {signum}, shutting down gracefully...")
        self.shutdown()
        sys.exit(0)

    def initialize(self) -> bool:
        """Initialize the Gopher client for video display"""
        try:
            # client reference to cpp code via the client lib and c/python bindings
            self.client = gopher_client.GopherClient()

            # ignore for now
            if self.dev_mode:
                self.client.enable_dev_mode(True)

            # Initialize with a different port than the main app
            success = self.client.initialize(f"{self.username}_video", self.recv_port)
            if not success:
                print("Failed to initialize video client")
                return False

            print(f"Video app initialized for {self.username}")
            print(f"Client IP: {self.client.get_ip()}, Port: {self.client.get_port()}")
            print(f"Target: {self.target_ip}:{self.target_port}")

            return True

        except Exception as e:
            print(f"Error initializing video app: {e}")
            return False

    def start_call(self) -> bool:
        """Start the video call"""
        if not self.client:
            print("Client not initialized")
            return False

        try:
            success = self.client.start_call(self.target_ip, self.target_port)

            if success:
                self.call_active = True
                print(f"Video call started successfully")
                return True
            else:
                print("Failed to start video call")
                return False

        except Exception as e:
            print(f"Error starting call: {e}")
            return False

    def run_video_display(self):
      """Main video display loop - MUST run in main thread on macOS"""
      if not self.call_active:
          print("No active call to display")
          return False

      print("Starting video display...")
      print("Press ESC or Q to end call, or close the window")

      # Give the call a moment to establish
      print("Waiting for call to establish...")
      time.sleep(3)

      try:
          print("Entering main video loop...")

          # Check if we're still in call before starting display
          if not self.client.is_in_call():
              print("Call not active, cannot start video display")
              return False

          # This should be a SINGLE blocking call that handles all video display
          # The C++ function will run its own event loop until the user closes the window
          print("Starting video display (this will block until window is closed)...")
          self.client.process_video_display()
          
          print("Video display function returned - window closed")

      except KeyboardInterrupt:
          print("Video display interrupted by keyboard")
      except Exception as e:
          print(f"Error in video display: {e}")
          import traceback
          traceback.print_exc()

      print("Video display completed")
      return True

    def shutdown(self):
        """Clean shutdown of the video app"""
        if not self.running:
            return

        if self.client:
            try:
                if hasattr(self.client, "request_shutdown"):
                    self.client.request_shutdown()

                # if self.client.is_in_call():
                #     print("Ending active call...")
                #     self.client.end_call()
                #     print("Call ended successfully")
                #     time.sleep(0.5)  # Give it time to clean up
            except Exception as e:
                print(f"Error ending call: {e}")

        print("Video app shutdown complete")

    def run(self):
        print("Initializing video app...")

        if not self.initialize():
            print("Failed to initialize video app")
            return False

        print("Starting video call...")
        if not self.start_call():
            print("Failed to start call")
            return False

        print("Running video display...")
        # Run video display in main thread (required for macOS)
        success = self.run_video_display()

        print("Video display completed, shutting down...")
        # self.shutdown()

        return success


##########################################################################################


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(description="Gopher Video Display App")
    parser.add_argument("target_ip", help="Target IP address for the call")
    parser.add_argument("target_port", type=int, help="Target port for the call")
    parser.add_argument("username", help="Username for the client")
    parser.add_argument("recv_port", type=int, help="Local port to bind")
    parser.add_argument("--dev", "-d", action="store_true", help="Enable dev mode")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")

    return parser.parse_args()


def main():
    """Main entry point"""
    args = parse_arguments()

    print(f"Starting Gopher video app...")
    print(f"Target: {args.target_ip}:{args.target_port}")
    print(f"Username: {args.username}")
    print(f"Dev mode: {args.dev}")
    print(f"Verbose: {args.verbose}")
    print(f"Recv Port: {args.recv_port}")
    print("=" * 50)

    # Create and run the video app
    app = GopherVideoApp(
        target_ip=args.target_ip,
        target_port=args.target_port,
        username=args.username,
        recv_port=args.recv_port,
        dev_mode=args.dev,
    )

    try:
        success = app.run()
        if success:
            print("Video app completed successfully")
            app.shutdown()
            sys.exit(0)
        else:
            print("Video app failed")
            sys.exit(1)
    except Exception as e:
        print(f"Unexpected error: {e}")
        import traceback

        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
