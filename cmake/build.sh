#!/bin/bash

# Gopher SwiftUI Build Script
# This script builds the SwiftUI app using Xcode

set -e

echo "🚀 Building Gopher SwiftUI App..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if Xcode is installed
if ! command -v xcodebuild &> /dev/null; then
    echo -e "${RED}❌ Xcode command line tools not found. Please install Xcode first.${NC}"
    exit 1
fi

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo -e "${RED}❌ This build script is for macOS only${NC}"
    exit 1
fi

# Build configuration
CONFIGURATION=${1:-Release}
PROJECT_NAME="gopher"
SCHEME_NAME="gopher"

echo -e "${YELLOW}📱 Building with configuration: $CONFIGURATION${NC}"

# Clean previous builds
echo "🧹 Cleaning previous builds..."
xcodebuild clean -project "$PROJECT_NAME.xcodeproj" -scheme "$SCHEME_NAME" -configuration "$CONFIGURATION" || true

# Build the app
echo "🔨 Building SwiftUI app..."
xcodebuild build -project "$PROJECT_NAME.xcodeproj" -scheme "$SCHEME_NAME" -configuration "$CONFIGURATION" -derivedDataPath "DerivedData"

# Check if build was successful
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Build completed successfully!${NC}"
    
    # Show build output location
    APP_PATH="DerivedData/Build/Products/$CONFIGURATION/$SCHEME_NAME.app"
    if [ -d "$APP_PATH" ]; then
        echo -e "${GREEN}📦 App bundle created at: $APP_PATH${NC}"
        
        # Show app size
        APP_SIZE=$(du -sh "$APP_PATH" | cut -f1)
        echo -e "${GREEN}📏 App size: $APP_SIZE${NC}"
    fi
else
    echo -e "${RED}❌ Build failed!${NC}"
    exit 1
fi

echo -e "${GREEN}🎉 Gopher SwiftUI app build complete!${NC}"
