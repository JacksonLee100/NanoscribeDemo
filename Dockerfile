FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential python3-dev python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install pybind11 numpy pytest

WORKDIR /app
COPY . .
RUN chmod +x build.sh && ./build.sh

CMD ["python3", "main.py"]
