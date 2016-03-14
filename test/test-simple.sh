#! /bin/bash -eu

SOURCE=/usr/bin/
PREVIOUS=/tmp/v0
NEW=/tmp/v1 

mkdir -vp $NEW
mkdir $PREVIOUS &&  rsync  -aa $SOURCE $PREVIOUS

cd ${NEW}  || exit 1
#find -delete 
tar c -C "${SOURCE}"  . | 
	ffuntar "$@" -pr "${PREVIOUS}"  || echo error


echo Missing files: 
( cd $SOURCE && find -type f -print0 |xargs -0r md5sum)  | 
	(cd  $NEW  && LANG=C md5sum -c ) |grep -v ': OK$'  || true

echo Extra files:
( cd $NEW && find -type f -print0 |xargs -0r md5sum)  | 
	(cd  $SOURCE  && LANG=C md5sum -c ) |grep -v ': OK$'  || true

