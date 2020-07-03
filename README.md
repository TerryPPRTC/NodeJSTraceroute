# NodeJSTraceroute

## To build
This project can be built on CentOS. But should be easy to be built on Mac.

Use node-gyp command.  The node-gyp need to be installed by npm.
```
npm install -g node-gyp
```
Then use node-gyp to build the project. Try these commands:
```
node-gyp configure
node-gyp generate
node-gyp build
node-gyp rebuild 
```
At last call the sample index.js:
```
node index.js
```

## To only try the c code part
If you want only test the C code, you can modify a function in mycimp.c to 'main'.
And you can set up a project on Mac, and debug the code.
Or you can build it on centOS using following command:
```
gcc -D_BSD_SOURCE myicmp.c poll.c -lm
```

## To understand the code

[中文代码讲解请参考](http://blog.pprtc.com/)!

