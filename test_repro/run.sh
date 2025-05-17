echo "Building image..."
podman build -t test_cont . > /dev/null
podman save -o tmp/test_cont.tar test_cont > /dev/null
echo "Success"

echo "====> Launching test for Podman"
podman load -i tmp/test_cont.tar > /dev/null
podman run test_cont
podman rmi -f test_cont > /dev/null

echo "====> Launching test for Docker"
docker load -i tmp/test_cont.tar > /dev/null
docker rmi -f test_cont > /dev/null

echo "Deleting image tar"
rm -rf tmp
