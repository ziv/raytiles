# Next Phase Architecture

### Data Flow

- Init streamer with lat, lon and config
- Complete configuration based on lat and lon
- Make input validation
- Create mesh for each zoom level
- Create arrays of data for fast access by zoom
- Create shared buffer of data that shared between services

Single configuration object splits into multiple options.



### Smart configuration object

All input validation is done in the configuration object on init.

```c++
// with defautls
raytiles::Config config{};

// with defaults and position
raytiles::Config config(lat, lon);
```

### An API to convert world coordinates to raylib coordinates

```c++
// from  Web Mercator to raylib 
Vector3 position = config.to_world(lat, lon); // [float, float] -> Vector3

// from raylib to Web Mercator
[lat, lon] = config.to_wm(position); // Vector3 -> [float, float]
```

### Initiate streamer with single configuration

The camera can be set at the init or later, but it must be set before calling update.

```c++
// init streamer
raytiles::Streamer streamer(config, camera);

// allow replace or set other camera in run time
streamer.set_camera(camera);

```

### Loop API

```c++
// in main loop
streamer.updater(offset);

// in 3d before all
streamer.draw();
```