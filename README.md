# OsmAnd Pebble Companion

OsmAnd Pebble Companion is a bridge application that connects the [OsmAnd](https://osmand.net/) navigation app on Android with Pebble smartwatches. It allows users to view essential navigation information and trip statistics directly on their wrist, reducing the need to check their phone while cycling, hiking, or driving.

## Features

- **Real-time Navigation**: View turn-by-turn instructions, distance to the next maneuver, and directional arrows.
- **Trip Statistics**: Monitor current speed, remaining distance to destination, and estimated time of arrival (ETA).
- **GPX Recording Control**: Start, stop, and pause GPX track recording directly from the watch. View live recording duration and distance.
- **Heart Rate Integration**: For Pebble models with heart rate sensors, the app can send heart rate data back to OsmAnd to be recorded with your track.
- **Reliable Background Operation**: Runs as an Android foreground service to maintain a stable connection between OsmAnd and your Pebble watch even when the phone screen is off.

## Project Structure

The project is organized as a monorepo containing two main components:

- **`osmAndCompanion`**: The Android companion app. It is built using Kotlin and Jetpack Compose, and it communicates with OsmAnd via its AIDL API and with the Pebble watch via the Pebble Kit.
- **`pebbleApp`**: The watchface/app for Pebble watches. It is written in C and is compatible with various Pebble models (Aplite, Basalt, Chalk, etc.).

## Build Instructions

### Android App

To build the Android companion app, you need Android Studio or the Android SDK installed.

```bash
cd osmAndCompanion
./gradlew assembleDebug
```

The resulting APK will be located in `osmAndCompanion/build/outputs/apk/debug/`.

### Pebble App

To build the Pebble app, you need the Pebble SDK (or Rebble SDK) installed and configured.

```bash
cd pebbleApp
pebble build
```

The resulting `.pbw` file will be located in `pebbleApp/build/`.

## Requirements

- An Android device with [OsmAnd](https://osmand.net/) (or OsmAnd+) installed.
- A Pebble smartwatch (Original, Steel, Time, Time Steel, Time Round, or Pebble 2).
- Pebble/Rebble app installed on the Android device.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
