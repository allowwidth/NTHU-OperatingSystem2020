../build.linux/nachos -f
../build.linux/nachos -mkdir /d0
../build.linux/nachos -mkdir /d1
../build.linux/nachos -mkdir /d2
../build.linux/nachos -cp num_100.txt /d0/f0
../build.linux/nachos -mkdir /d0/aa
../build.linux/nachos -mkdir /d0/bb
../build.linux/nachos -mkdir /d1/cc
../build.linux/nachos -mkdir /d1/dd
../build.linux/nachos -cp num_100.txt /d0/bb/f1
../build.linux/nachos -cp num_100.txt /d0/bb/f2
../build.linux/nachos -cp num_100.txt /d0/bb/f3
../build.linux/nachos -cp num_100.txt /d0/bb/f4
../build.linux/nachos -cp num_100.txt /d1/cc/f1
../build.linux/nachos -cp num_100.txt /d1/cc/f2
../build.linux/nachos -cp num_100.txt /d1/cc/f3
../build.linux/nachos -cp num_100.txt /d1/cc/f4
echo "========================================"
../build.linux/nachos -lr /d0
echo "========================================="
../build.linux/nachos -r /d0/bb/f1
../build.linux/nachos -lr /d0/bb
echo "========================================="
../build.linux/nachos -rr /d0
../build.linux/nachos -lr /
echo "========================================="
../build.linux/nachos -rr /d1
../build.linux/nachos -lr /
echo "========================================="
