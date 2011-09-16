TSXS=tsxs

%.so: %.c
	$(TSXS) -c $< -o $@

all: add_header.so

install: all
	$(TSXS) -i -o add_header.so

clean:
	rm -f *.lo *.so
