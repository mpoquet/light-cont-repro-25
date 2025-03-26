podman pull ubuntu
container_id=$(podman run -d ubuntu:latest sleep 3600)
podman export -o ubuntu_fs.tar $container_id
podman rm -f $container_id
