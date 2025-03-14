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

### preview
![v0](image0.png)
![v1](image1.png)
![v2](animation.gif)