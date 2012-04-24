mkdir -p build  # only make folder if it does not exist
cd build
cmake ../
make
if [ $? -eq 0 ] ; then
    cd ..
    #./build/scriptbots -h -v -e 2 -n 1
    #./build/scriptbots -h -v -e 2 -n 12
    numactl --interleave=all ./build/scriptbots -h -v -e 2 -n 12
else
    cd ..
fi
