FROM alpine:3.19

RUN apk add --no-cache gcc musl-dev make

WORKDIR /app

COPY . /app

RUN make clean && make

EXPOSE 5050

CMD ["./server_forza4", "5050"]

