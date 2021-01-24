../build.linux/nachos -f
../build.linux/nachos -mkdir /t0 -d f
../build.linux/nachos -mkdir /t1 -d f
../build.linux/nachos -mkdir /t2 -d f
../build.linux/nachos -cp num_100.txt /t0/f1 -d f
../build.linux/nachos -l /
echo "========================================="
../build.linux/nachos -l /t0
echo "========================================="
../build.linux/nachos -lr /
