#! /bin/bash

cd Plugin && make clean && make
cp RenderingPlugin.{dll,pdb} ../../vlc-unity/Assets/Plugins/x86_64 -f