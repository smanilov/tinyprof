if [ "$#" -ne 1 ] ; then
  echo "Usage: $0 <file-to-add-profiling-info-to>" >&2
  exit 1
fi

if ! [ -e "$1" ] ; then
  echo "Error: $1 not found" >&2
  exit 1
fi

if ! [ -f "$1" ] ; then
  echo "Error: $1 is not a file" >&2
  exit 1
fi

SELF_DIR=$(dirname $0)

fullname=$1
extension="${fullname##*.}"
filename="${fullname%.*}"
newname=$filename-prof.$extension
echo "Copying $fullname to $newname..."
cp $fullname $newname 
echo "Annotating $newname..."
$SELF_DIR/tinyprof $newname -- -I/usr/local/lib/clang/3.7.1/include/ -Itest
echo "Done!"
