.PHONY: all server clean docker-up docker-down test

all: server

server:
	$(MAKE) -C server

clean:
	$(MAKE) -C server clean

docker-up:
	docker compose up --build -d server

docker-down:
	docker compose down

test:
	python test/test_race_conditions.py
