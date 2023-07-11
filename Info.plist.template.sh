#!/usr/bin/env bash

: ${DEVELOPMENT_LANGUAGE:?parameter missing, eg: English}
: ${EXECUTABLE_NAME:?parameter missing, eg:filesystem_plugin}
: ${PRODUCT_BUNDLE_IDENTIFIER:?parameter missing, eg: org.videolan.vlc.filesystem-plugin}
: ${PRODUCT_NAME:?parameter missing, eg: filesystem_plugin}
: ${CURRENT_PROJECT_VERSION:?parameter missing, eg: 4.0}

# CONVERT_PLIST <input file> <output file>
# Convert a plist file into binary1 format in order to put it
# into an Apple bundle.
CONVERT_PLIST()
{
    INPUT=$1
    OUTPUT=$2
    if [ which plutil > /dev/null 2>&1 ]; then
        plutil -convert binary1 -o "$OUTPUT" -- "$INPUT"
    else
        plistutil -o "$OUTPUT" -i "$INPUT"
    fi
}

cat <<EOF | plutil -convert binary1 -o - -- -
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>${DEVELOPMENT_LANGUAGE}</string>
	<key>CFBundleExecutable</key>
	<string>${EXECUTABLE_NAME}</string>
	<key>CFBundleName</key>
  <string>${DISPLAY_NAME}</string>
	<key>CFBundleIdentifier</key>
	<string>${PRODUCT_BUNDLE_IDENTIFIER}</string>
	<key>CFBundleName</key>
	<string>${PRODUCT_NAME}</string>
	<key>CFBundleVersion</key>
	<string>${CURRENT_PROJECT_VERSION}</string>
	<key>CFBundlePackageType</key>
	<string>FMWK</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
</dict>
</plist>
EOF
