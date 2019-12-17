#! /bin/bash

cd Assets/VLC-Unity-Windows/Plugins/Source && make clean && make
cp RenderingPlugin.{dll,pdb} ../x86_64 -f