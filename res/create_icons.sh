#!/bin/bash

for img in *.svg; do
 gtk4-encode-symbolic-svg $img 56x56
done
