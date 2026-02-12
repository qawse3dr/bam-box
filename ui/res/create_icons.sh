#!/bin/bash

for img in *.svg; do
   gtk4-encode-symbolic-svg $img 48x48
   convert "${img%.svg}.symbolic.png" -fill white -opaque black "${img%.svg}.png"
   rm "${img%.svg}.symbolic.png"
done
