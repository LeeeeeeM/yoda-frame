### install sdl2
```
brew install sdl2
```

### build yoga
```
cd yoga/yoga

mkdir build

cd build

cmake ..

make
```

### build quickjs
```
cd slowjs

cmake -S . --preset=default -D CMAKE_BUILD_TYPE=Release

```


### build project
```
mkdir build

cd build

cmake ..

make
```

### build react-reconciler
```
cd yoda-react-reconciler

pnpm install

npm run build

move dist/*.js ../js/
```

### run
```
cd build

// rename demo.js to your own file, e.g. demo.txt
./main ../js/demo.txt   
```

### preview
![v0](image0.png)
![v1](image1.png)
![v2](animation.gif)
![v3](reconciler.gif)