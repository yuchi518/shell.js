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

### Installation & Run

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

### Examples
```sh
# cd /path/to/shell.js
# examples/ex1.js
hello!! j= 3
```
or
```sh
# jssh examples/ex1.js
hello!! j= 3
```

### ToDo
1. Add more libs (network, file, reg).
2. Add into OpenWRT.

License
---
GPLv2
