# NOTE: run this from dir above docker!
docker run -it --rm \
    -v $(pwd):/home/relax/registration \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -e DISPLAY=$DISPLAY \
    --name qap-register-c \
    --net host \
    --privileged \
    --runtime=nvidia \
    qap-register 
