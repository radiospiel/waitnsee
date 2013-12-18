default: build test

build:
	make -C src
	
install: test
	cp -r bin/* /usr/local/bin

test:
	# cd tests && ./run.sh
