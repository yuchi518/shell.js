# Shell.js (jssh)
JavaScript shell

### Version
0.0.2

### Tech
Shell.js uses a number of open source projects to work properly:

* [v7] - JavaScript interpreter. (https://github.com/cesanta/v7)
* [mongoose] - Network library. (https://github.com/cesanta/mongoose)


### System Requirements

- CMake (https://cmake.org)
- Mac OS (Verified), Linux, etc.

### Installation

```sh
# cd /path/to/shell.js
# cmake .
# make install
```
Where is jssh?
```sh
# which jssh
/usr/local/bin/jssh
```

### Run interactive mode
You can run in interactive mode like most shell script language.
```sh
# jssh
Shell.js 0.1
>>> var a = 10
>>> print (a)
10 
>>> hi
err: Exec exception, result: ffff000001eaae88
>>> ^C
```

### Run Examples
If you clone the project in folder /path/to/shell.js, there
are some examples in sub folder /path/to/shell.js/examples.
Make sure all files in sub folder /path/to/shell.js/examples are
executable.
```sh
# cd /path/to/shell.js
# examples/ex1.js
hello!! j= 3
```
or (If they are not executable files.)
```sh
# jssh examples/ex1.js
hello!! j= 3
```

### ToDo
- Add more libs (network, file, regex).
- Add into OpenWRT.

License
---
GPLv2
