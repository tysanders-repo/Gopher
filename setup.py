import os
import glob
import shutil
from setuptools import setup

# detect Homebrew prefix (handles both Intel and Apple Silicon)
brew_prefix = os.popen('brew --prefix').read().strip()

# Find all OpenCV libraries dynamically
opencv_libs = glob.glob(os.path.join(brew_prefix, 'lib', 'libopencv_*.dylib'))

# Copy the extension module to where py2app expects it
extension_src = 'build/gopher_client.cpython-313-darwin.so'
extension_dst = 'gopher_client.so'
if os.path.exists(extension_src):
    shutil.copy2(extension_src, extension_dst)
    print(f"Copied {extension_src} to {extension_dst}")
else:
    print(f"Warning: {extension_src} not found!")

APP = ['gopher_status_app.py']
OPTIONS = {
    # your pure-Python dependencies
    'packages': [],
    'includes': ['rumps', 'gopher_client'],  # Tell py2app about your extension
    'excludes': [
        'test',  # the stdlib test/ package
        'decimaltestdata',  # decimal tests
        'xmltestdata',  # xml tests
        'regrtestdata',  # regression tests
        'cjkencodings',  # tkinter test data
        'archivetestdata',
        'translationdata',
        'wheeldata',
        # Note: removed 'packaging', 'pkg_resources', 'importlib_metadata' 
        # as they're needed by the app
    ],
    # everything listed here winds up in Contents/Resources
    'resources': [
        'build/gopherd',  # your C++ daemon (make sure it's executable)
        'build/libgopher_client_lib.a',  # static library (if you need it)
        'gopher_client.so',  # your Python extension (copied above)
        'gopher_video_app.py',  # video app script - put it directly in Resources
    ],
    # FFmpeg and OpenCV dylibs - py2app will handle path rewriting
    'frameworks': [
        # FFmpeg dylibs
        os.path.join(brew_prefix, 'lib', 'libavcodec.dylib'),
        os.path.join(brew_prefix, 'lib', 'libavformat.dylib'),
        os.path.join(brew_prefix, 'lib', 'libavutil.dylib'),
        os.path.join(brew_prefix, 'lib', 'libswscale.dylib'),
        os.path.join(brew_prefix, 'lib', 'libavdevice.dylib'),
        os.path.join(brew_prefix, 'lib', 'libswresample.dylib'),
    ] + opencv_libs,  # Add all OpenCV libraries
    # turn off auto-framework emulation
    'argv_emulation': False,
    # This is crucial - tells py2app to fix library paths
    'strip': False,  # Don't strip symbols, needed for debugging
    'optimize': 0,   # No optimization to avoid issues
    'plist': {
        'CFBundleName': 'Gopher',
        'CFBundleIdentifier': 'com.tysanders.gopher',
        'CFBundleVersion': '0.1.0',
        'CFBundleShortVersionString': '0.1.0',
        'LSMinimumSystemVersion': '10.12.0',
        'NSHighResolutionCapable': True,
        # Allow unsigned code execution (for development)
        'com.apple.security.cs.allow-unsigned-executable-memory': True,
        'com.apple.security.cs.disable-library-validation': True,
    },
}

setup(
    name='Gopher',
    app=['gopher_status_app.py'],
    options={'py2app': OPTIONS},
    setup_requires=['py2app'],
    install_requires=['rumps'],
)