#########################################################################
# File Name: test.sh
# Author: wangjx
# mail: wangjianxing5210@163.com
# Created Time: 2019年05月08日 星期三 13时20分50秒
#########################################################################
#!/bin/bash
make clean
make
insmod simplefs.ko
dd if=/dev/zero of=image bs=10M count=1
./mkfs-simplefs image
./parse-simplefs image
mount -o loop -t simplefs image /mnt
echo 333 >/mnt/aaa
./parse-simplefs image
echo 444 > /mnt/bbb
./parse-simplefs image
