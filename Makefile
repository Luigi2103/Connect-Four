.PHONY: all server clean tsan asan docker-up docker-down test

all: server

server:
	$(MAKE) -C server

tsan:
	$(MAKE) -C server tsan

asan:
	$(MAKE) -C server asan

clean:
	$(MAKE) -C server clean

docker-up:
	docker compose up --build -d server

docker-down:
	docker compose down

test:
	python test/test_race_conditions.py
