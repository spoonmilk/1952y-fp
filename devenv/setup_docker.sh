#! /bin/bash

cd "$(dirname "$0")" || exit

if [ -d ../gem5 ]; then
    echo "Directory ../gem5 ($(realpath ../gem5)) already exists. Please save your work and delete it before running this script"
    exit
fi

IMAGE="cs1952y-final:latest"
GEM5_REPO="https://github.com/browncs1952y/gem5-assignments-stencil"

while test "$#" -ne 0; do
  case "$1" in
    --gem5-repo)
      GEM5_REPO=$2
      shift 2
      ;;
    *)
      echo "Usage: setup_docker.sh [--gem5-repo <url>]"
      exit
      ;;
  esac
done

echo "Building image $IMAGE (this will take a while)..."
docker build --build-arg GEM5_REPO="$GEM5_REPO" -t "$IMAGE" . || (echo "Error building image." && exit 1)

echo "Starting docker container to copy over directories"
cname="cs1952y-final-container-temp"
docker run -d --name "$cname" "$IMAGE" || exit

echo "Copying over gem5 directory"
docker cp "$cname:/home/cs1952y-user/gem5" ../gem5 || exit

docker stop "$cname"
docker rm "$cname"

echo "SUCCESS!"
echo ""
echo "Your work will be saved locally in the ../gem5 directory."
echo "To point it at your own repo:"
echo "  cd ../gem5"
echo "  git remote set-url origin <your-repo-url>"
