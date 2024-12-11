
FROM python:3.11-alpine

WORKDIR /opt/showorb

RUN apk update && apk add --no-cache mosquitto
RUN apk update && apk add --no-cache --virtual .build-deps \
	gcc \
   make \
	libc-dev \
	linux-headers \
   mosquitto-dev \
;

# Copy app files.
COPY ./show.c /opt/showorb
COPY ./Makefile /opt/showorb

RUN make

RUN apk del .build-deps

CMD ["/opt/showorb/showorb", "/etc/showorb/showorb.conf"]

