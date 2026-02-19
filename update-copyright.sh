#!/bin/sh

# This file is part of Oak Video Editor - A fork of original project Olive 
#

if [ "$#" -ne 2 ]
then
  echo "Usage: $0 [old-year] [new-year]"
  exit 1
fi

find . -type f -exec sed -i "s/$1 Olive CE Team/$2 Olive CE Team/g" {} \;
