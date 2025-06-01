../minimalist_runtime/light-cont --path ./sysbench.oci.tar --extractpath ./bench-cpu/rootfs --run "nothing"
cp -r ./bench-cpu/rootfs ./bench-fileio
cp -r ./bench-cpu/rootfs ./bench-memory
cp -r ./bench-cpu/rootfs ./bench-launchtime