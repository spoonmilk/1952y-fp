#! /bin/bash

CONTAINER="cs1952y-final-container"
IMAGE="cs1952y-final:latest"

cd "$(dirname "$0")" || exit

if [ ! -d ../gem5 ]; then
    echo "Directory ../gem5 ($(realpath ../gem5)) does not exist. Have you run setup_docker.sh?"
    exit
fi
if ! docker images > /dev/null 2>&1; then
    echo "Make sure docker is running!"
    exit
fi
if ! docker image inspect $IMAGE > /dev/null 2>&1; then
    echo "$IMAGE docker image does not exist. Have you run setup_docker.sh?"
    exit
fi

clean=false

while test "$#" -ne 0; do
  case "$1" in
    -C | --clean)
      clean=true
      shift
      ;;
    *)
      echo "Usage: run_docker.sh [-C|--clean]"
      exit
      ;;
  esac
done

set_xhost() {
  if ! command -v xhost &> /dev/null; then
    echo "xhost command not found. X11 forwarding might not work."
    return
  fi

  case "$(uname)" in
    "Linux")
      xhost +local:docker
      ;;
    "Darwin")
      xhost +localhost
      ;;
    "CYGWIN"*|"MINGW32"*|"MSYS"*|"MINGW"*)
      ;;
  esac
}

remove_containers() {
  echo "Removing all existing cs1952y-final containers..."
  docker ps -a -f name=$CONTAINER --format "{{.ID}}" | while read -r line; do
      docker rm --force "$line"
  done
}

start_new_container() {
  echo "Starting a new container..."
  docker run -itd \
      --name $CONTAINER \
      -v $(realpath ../gem5):/home/cs1952y-user/gem5 \
      $displayarg$x11mountarg \
      $IMAGE bash

  docker exec -it $CONTAINER bash
}

# set up X11 forwarding
displayarg=""
x11mountarg=""
case "$(uname)" in
  "Linux")
    displayarg="-e DISPLAY "
    x11mountarg="-v /tmp/.X11-unix:/tmp/.X11-unix "
    ;;
  "Darwin")
    displayarg="-e DISPLAY=host.docker.internal:0 "
    ;;
  "CYGWIN"*|"MINGW32"*|"MSYS"*|"MINGW"*)
    displayarg="-e DISPLAY=host.docker.internal:0.0 "
    ;;
  *)
    echo "Unsupported OS $(uname) for X11 forwarding -- please contact course staff"
    exit
    ;;
esac

set_xhost

# start docker container
if $clean; then
    remove_containers && start_new_container
else
    exited_container=$(docker ps -a -f status=exited -f name=$CONTAINER --format "{{.ID}}")
    if [[ -n "$exited_container" ]]; then
        echo "Removing exited container $exited_container"
        docker rm $exited_container
    fi

    existing_container=$(docker ps -f status=running -f ancestor="$IMAGE" -f volume="/home/cs1952y-user/gem5" --no-trunc --format "{{.CreatedAt}},{{.ID}}" | sort -r | head -n 1)
    if [[ -n "$existing_container" ]]; then
        created_at=${existing_container%%,*}
        cfields=${existing_container##*,}
        ecid=${cfields:0:12}
        echo "* Using running container $ecid, created $created_at"
        echo "- To start a new container, exit then 'docker kill $ecid' then rerun this script"
        docker exec -it $ecid bash
    else
        start_new_container
    fi
fi
