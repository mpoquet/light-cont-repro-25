PROG=light-cont
CC=gcc

$(PROG): rootfs light-cont.c 
	gcc light-cont.c -o $(PROG)

ubuntu_fs.tar:
	./pull_ubuntufs.sh

rootfs: ubuntu_fs.tar
	tar -xvf /tmp/light-cont/ubuntu_fs.tar -C /tmp/light-cont/rootfs

clean:
	rm -rf $(PROG)
	rm -rf /tmp/light-cont/ubuntu_fs.tar
	rm -rf /tmp/light-cont/rootfs
