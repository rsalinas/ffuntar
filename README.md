# ffuntar
Flash-friendly untar tool

ffuntar is a tool conceived to repeatedly extracting tarballs that differ in 
just a small  fraction.

Unlike standard tar,  ffuntar is run with a  "reference directory" parameter,
and will, on the fly, try to open each file there.  If it exists, it will 
compare it byte by byte and just hard-link it if the new file is equal to the 
old reference data.  This means no flash write cycles at all, and allows 
for easy keeping of multiple versions, as long as the data is read-only.

It also reports the percentage of reused bytes, that did not need to be
rewritten.  In software building systems, it can be a hint of the "reproducibility
rate" if executed between two builds of the same inputs. 

= Example =

mkdir $BASE/v1
cd $BASE/v1
wget -O - http://.../current.tar | ffuntar  -r $BASE/latest
cd $BASE
ln -snvf  v1  latest

= References = 

It uses libarchive and is based in an example that comes with it, untar.c.

http://www.libarchive.org/

= Author = 

Raúl Salinas-Monteagudo <rausalinas@gmail.com>
