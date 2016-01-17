# Shell.js (jssh)
JavaScript shell

### Version
0.0.5

### Tech
Shell.js uses a number of open source projects to work properly:

* [v7] - JavaScript interpreter. (https://github.com/cesanta/v7)
* [mongoose] - Network library. (https://github.com/cesanta/mongoose)
* [uthash] - Hash library. (https://github.com/troydhanson/uthash)


### System Requirements

- CMake (https://cmake.org), testing in version 3.3 and later.
- Mac OS or Linux, etc.

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

### Buildin APIs

- Env: ls, cd, pwd, realpath, etc.
```sh
Shell.js 0.1
>> cd("/Volumes")
>>> print(pwd())
/Volumes 
>>> print(ls("."))
["."] 
>>> print(realpath(ls(".")))
["/Volumes"] 
>>> print(realpath(ls("./*")))
["/Volumes/Backup","/Volumes/Develop","/Volumes/Document","/","/Volumes/都是電影 嗎"] 
>>> print(ls("./*"))
["./Backup","./Develop","./Document","./OS","./都是電影 嗎"] 
>>> 
```

- Env: cat, echo
```sh
Shell.js 0.1
>>> print(cd("/Volumes/Develop"))
/Volumes/Develop 
>>> print(ls("a.txt"))
undefined 
jsc_file: problem with a.txt (no match of pattern), stopping early
>>> echo("abcdefg", "a.txt")
>>> print(ls("a.txt"))
a.txt 
>>> print(cat("a.txt"))
abcdefg 
>>> 
```

### ToDo
- Add more libs (network, file, regex).
- Add into OpenWRT.

License
---
GPLv2
