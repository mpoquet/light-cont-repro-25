PROG=light-cont
CC=gcc

$(PROG): rootfs light-cont.c 
	gcc light-cont.c -o $(PROG)

ubuntu_fs.tar:
	./pull_ubuntufs.sh

rootfs: ubuntu_fs.tar
	mkdir rootfs
	tar -xvf ubuntu_fs.tar -C ./rootfs

clean:
	rm -rf $(PROG)
	rm -rf ubuntu_fs.tar
	rm -rf rootfs
