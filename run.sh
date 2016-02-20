fullname=$1
extension="${fullname##*.}"
filename="${fullname%.*}"
newname=$filename-prof.$extension
echo "Copying $fullname to $newname..."
cp $fullname $newname 
echo "Annotating $newname..."
./tinyprof $newname -- -std=c++11 -I/usr/local/lib/clang/3.7.0/include/ -Itest
echo "Done!"
