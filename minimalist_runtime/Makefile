PROG=light-cont
CC=gcc

$(PROG): light-cont.c 
	gcc light-cont.c -o $(PROG) -larchive -lcap

#ubuntu_fs.tar:
#	./pull_ubuntufs.sh

#rootfs: ubuntu_fs.tar
#	tar -xvf ubuntu_fs.tar -C ./rootfs

clean:
	rm -rf $(PROG)
#	rm -rf ubuntu_fs.tar
#	rm -rf ./rootfs
