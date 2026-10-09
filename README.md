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
  model

options:
  --output OUTPUTFILEPATH  -  needed for font & model
  --cookie COOKIE          -  needed for model (you can also use RAF_COOKIE environment variable)
```

### example usage
```bash
./build/raf font 8075237774 --output RobotoMono-Light.ttf
./build/raf model 7064399097 --output JailCell.rbxm --cookie ".ROBLOSECURITY=_|WARNING:-DO-NOT-SHARE-THIS..."
# NOTE the cookie can just start with '_|WARNING:...' and raf will prepend '.ROBLOSECURITY=' for you
```
