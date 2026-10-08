# raf

raf is a Roblox Asset Fetcher

## generate build files
```bash
cmake -B build -S .
```

## build
```bash
cmake --build build
```

## usage
```
usage: raf TYPE ASSETID [options]

types:
  font

options:
  --output OUTPUTFILEPATH
```

### example usage
```bash
./build/raf font 8075237774 --output RobotoMono-Light.ttf
```
