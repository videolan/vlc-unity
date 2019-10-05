#! /bin/bash

cd Plugin && make
cp RenderingPlugin.{dll,pdb} ../../vlc-unity/Assets/Plugins/x86_64 -f