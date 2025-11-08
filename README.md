# SHT hash table library

Copyright 2025 Ian Pilcher <<arequipeno@gmail.com>>

## Documentation

See:

* [*Robin Hood hashing*][1], for an overview of the Robin Hood hash table
  algorithm and concepts,

* [*SHT Hash Table*][2], for an overview of the library and API documentation,
  and

* [*PSL Limits*][3], for a description of how the library enforces probe
  sequence length (PSL) limits.

## Building and installing

The library consists of a single file, so building it is straightforward.

```
$ cd src

$ gcc -O2 -Wall -Wextra -Wcast-qual -Wcast-align=strict -shared -fPIC \
	-Wl,-soname,libsht.so.0.1 -o libsht.so.0.1.0 sht.c
```

Installation requires `root` privileges.  (Adjust the library path as necessary
for the target distribution and architecture.)

Install the library file and symlinks.

```
$ sudo cp src/libsht.so.0.1.0 /usr/local/lib64/
$ sudo ln -s libsht.so.0.1.0 /usr/local/lib64/libsht.so.0.1
$ sudo ln -s libsht.so.0.1.0 /usr/local/lib64/libsht.so
```

Configure the dynamic linker (if necessary) and update its cache.

```
$ echo /usr/local/lib64 | sudo tee /etc/ld.so.conf.d/local.conf
/usr/local/lib64

$ sudo ldconfig
```

If desired, installed the header file.

```
$ sudo cp src/sht.h /usr/local/include
```


[1]: https://github.com/ipilcher/sht/blob/main/docs/robin-hood.md
[2]: https://ipilcher.github.io/sht/
[3]: https://github.com/ipilcher/sht/blob/main/docs/psl-limits.md
