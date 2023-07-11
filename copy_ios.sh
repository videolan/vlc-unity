#!/usr/bin/env bash
# Authors: Alexandre Janniaux <ajanni@videolabs.io>

VLC_INSTALL_DIR="$1"
ARCH="$2"
SCRIPT_DIR="$(cd "$(dirname "$0")"; pwd -P)"

IOS_DIR="${SCRIPT_DIR}/Assets/VLCUnity/Plugins/iOS/"
mkdir -p "${IOS_DIR}/${ARCH}/vlc/plugins/" \
    ${IOS_DIR}/${ARCH}/vlccore.framework/ \
    ${IOS_DIR}/${ARCH}/vlc.framework/

cp "${VLC_INSTALL_DIR}/lib/libvlc.dylib" "${IOS_DIR}/${ARCH}/vlc.framework/vlc"
cp "${VLC_INSTALL_DIR}/lib/libvlccore.dylib" "${IOS_DIR}/${ARCH}/vlccore.framework/vlccore"

env -i \
      DEVELOPMENT_LANGUAGE=English \
      EXECUTABLE_NAME="vlc" \
      PRODUCT_BUNDLE_IDENTIFIER="org.videolan.vlc" \
      PRODUCT_NAME="vlc" \
      CURRENT_PROJECT_VERSION="4.0" \
      "${SCRIPT_DIR}/Info.plist.template.sh" > "${IOS_DIR}/${ARCH}/vlc.framework/Info.plist"

install_name_tool -id "@rpath/vlc.framework/vlc" "${IOS_DIR}/${ARCH}/vlc.framework/vlc"
install_name_tool -change "@rpath/libvlccore.dylib" "@rpath/vlccore.framework/vlccore" "${IOS_DIR}/${ARCH}/vlc.framework/vlc"
install_name_tool -id "@rpath/vlccore.framework/vlccore" "${IOS_DIR}/${ARCH}/vlccore.framework/vlccore"

env -i \
      DEVELOPMENT_LANGUAGE=English \
      EXECUTABLE_NAME="vlccore" \
      PRODUCT_BUNDLE_IDENTIFIER="org.videolan.vlccore" \
      PRODUCT_NAME="vlccore" \
      CURRENT_PROJECT_VERSION="4.0" \
      "${SCRIPT_DIR}/Info.plist.template.sh" > "${IOS_DIR}/${ARCH}/vlccore.framework/Info.plist"

mkdir -p "${SCRIPT_DIR}/Assets/VLCUnity/Plugins/iOS/${ARCH}/VLCUnityPlugin.framework/"
cp "${SCRIPT_DIR}/build_${ARCH}/PluginSource/libVLCUnityPlugin.1.dylib" "${SCRIPT_DIR}/Assets/VLCUnity/Plugins/iOS/${ARCH}/VLCUnityPlugin.framework/VLCUnityPlugin"
install_name_tool -id "@rpath/VLCUnityPlugin.framework/VLCUnityPlugin" "${SCRIPT_DIR}/Assets/VLCUnity/Plugins/iOS/${ARCH}/VLCUnityPlugin.framework/VLCUnityPlugin"
install_name_tool -change "@rpath/libvlc.dylib" "@rpath/vlc.framework/vlc" "${SCRIPT_DIR}/Assets/VLCUnity/Plugins/iOS/${ARCH}/VLCUnityPlugin.framework/VLCUnityPlugin"

env -i \
      DEVELOPMENT_LANGUAGE=English \
      EXECUTABLE_NAME="VLCUnityPlugin" \
      PRODUCT_BUNDLE_IDENTIFIER="org.videolan.VLCUnityPlugin" \
      PRODUCT_NAME="VLCUnityPlugin" \
      CURRENT_PROJECT_VERSION="4.0" \
      "${SCRIPT_DIR}/Info.plist.template.sh" > "${IOS_DIR}/${ARCH}/VLCUnityPlugin.framework/Info.plist"

PLUGINS=$(find "${VLC_INSTALL_DIR}/lib/vlc/plugins/" -name '*.dylib')

for plugin in ${PLUGINS}; do
  file="$(basename ${plugin})"
  file="${file#lib}"
  framework="${IOS_DIR}/${ARCH}/vlc/plugins/${file%.*}.framework/"
  mkdir -p "${framework}"
  cp "${plugin}" "${framework}/${file%.*}"
  install_name_tool -id "@rpath/${file%.*}.framework/${file%.*}" "${framework}/${file%.*}"
  install_name_tool -change "@rpath/libvlccore.dylib" "@rpath/vlccore.framework/vlccore" "${framework}/${file%.*}"
  env -i \
      DEVELOPMENT_LANGUAGE=English \
      EXECUTABLE_NAME="${file%.*}" \
      PRODUCT_BUNDLE_IDENTIFIER="org.videolan.plugin.$(echo "${file%.*}" | tr _ -)" \
      PRODUCT_NAME="${file%.*}" \
      CURRENT_PROJECT_VERSION="4.0" \
      "${SCRIPT_DIR}/Info.plist.template.sh" > "${framework}/Info.plist"
done
