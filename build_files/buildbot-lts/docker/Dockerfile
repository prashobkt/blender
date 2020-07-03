FROM ubuntu:18.04

RUN apt update && apt upgrade -y
RUN apt install -y \
    gosu \
    wget \
    openssh-client \
    build-essential \
    libffi-dev \
    libssl-dev \
    python3-dev \
    python3-pip
    

ENV BUILDBOT_VERSION 2.5.0
RUN pip3 --no-cache-dir install --upgrade pip && pip --no-cache-dir install \
    buildbot[bundle,tls]==$BUILDBOT_VERSION \
    buildbot-docker-swarm-worker

COPY buildbot.tac /var/lib/buildbot/
COPY docker-entrypoint.sh /usr/local/bin/
COPY master.cfg /etc/buildbot/
COPY bin/create_checksum.sh /var/lib/buildbot/bin/
COPY bin/upload_file.sh /var/lib/buildbot/bin/
COPY .ssh/id_rsa /var/lib/buildbot/.ssh/

RUN adduser --home /var/lib/buildbot --disabled-password --gecos '' buildbot
WORKDIR /var/lib/buildbot
RUN ln -s /etc/buildbot/master.cfg

VOLUME /var/lib/buildbot
EXPOSE 8010
EXPOSE 9989

ENTRYPOINT ["docker-entrypoint.sh"]
CMD ["twistd", "--pidfile=", "--nodaemon", "--python=buildbot.tac"]