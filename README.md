# Shell.js (jssh)
JavaScript shell

### Version
0.0.2

### Tech
Shell.js uses a number of open source projects to work properly:

* [v7] - JavaScript interpreter. (https://github.com/cesanta/v7)
* [mongoose] - Network library. (https://github.com/cesanta/mongoose)


### System Requirements

1. CMake (https://cmake.org)
2. Mac OS (Verified), Linux, etc.

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
1. Add more libs (network, file, regex).
2. Add into OpenWRT.

License
---
GPLv2
