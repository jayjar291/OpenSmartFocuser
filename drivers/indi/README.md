# INDI Driver

Linux/macOS driver for OpenSmartFocuser using the [INDI Library](https://indilib.org).

## Build Requirements
- `libindi-dev`
- `cmake >= 3.10`
- `g++ >= 9`

## Build
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

## Usage
Load the driver in your INDI client (KStars/Ekos, Cartes du Ciel, etc.) and select **OpenSmartFocuser**.
