# Raytiles

## Tiles Sizes

$TileWidth_{meters} = \frac{C \cdot \cos(\phi)}{2^z}$

where:

- C is the circumference of the Earth (approximately 40,075 km)
- φ is the latitude
- z is the zoom level

For our fixed size tiles of 256x256 pixels, we can calculate the tile width in meters at different zoom levels (z) and
latitudes (φ):

$Resolution = \frac{C \cdot \cos(\phi)}{256 \cdot 2^z}$

### Example Calculations

- Equator $\phi=0$
- TLV $\phi=\cos(32^\circ)$

| Zoom Level (z) | Latitude (φ) | Resolution (meters/pixel) | Tile Width (meters) |
|----------------|--------------|---------------------------|---------------------|
| 15             | Equator      | 4.77                      | 1222                |
| 15             | TLV          | 4.05                      | 1036                |
| 14             | Equator      | 9.54                      | 2445                |
| 14             | TLV          | 8.10                      | 2072                |
| 13             | Equator      | 19.11                     | 4890                |
| 13             | TLV          | 16.20                     | 4144                |
| 12             | Equator      | 38.22                     | 9780                |
| 12             | TLV          | 32.40                     | 8288                |
| 11             | Equator      | 76.44                     | 19560               |
| 11             | TLV          | 64.80                     | 16576               |
| 10             | Equator      | 152.88                    | 39120               |
| 10             | TLV          | 129.60                    | 33152               |
| 9              | Equator      | 305.77                    | 78240               |
| 9              | TLV          | 259.20                    | 66304               |

## Distances

From commercial aircraft, at 43K feets (~13Km) the distance to the horizon is approximately 250 miles (~400km).

$$(R + h)^2 = R^2 + d^2$$

Where:

- R is the radius of the Earth (approximately 6371 km)
- h is the height of the observer
- d is the distance to the horizon

$$d = \sqrt{2Rh + h^2}$$

For $(R ≫ h)$, we can simplify the formula to:

$$d \approx \sqrt{2Rh}$$

$$d \approx {3.57}\cdot10^3\sqrt{h}$$

**We limit the GPU pixels rendeing by limit the far plane to the horizon distance.**

## Tile Zoom Level Selection

Take the camera `Y` position and calculate the distance to the horizon `d`:

$$d \approx {3.57}\cdot10^3\sqrt{Y}$$

From max rendering distance of 400km, we set the distance thresholds for each zoom level to be half of the previous zoom
level:

| Zoom Level (z) | Distance Threshold (km) |
|----------------|-------------------------|
| 9              | 400                     |
| 10             | 200                     |
| 11             | 100                     |
| 12             | 50                      |
| 13             | 25                      |
| 14             | 12.5                    |
| 15             | 6.25                    |

For example, if the camera is at 13km height, the distance to the horizon is approximately 400km. Therefore, we will
render tiles up to zoom level 9, and for tiles that are closer than 200km, we will render zoom level 10, and so on.

---

With the sight distance, we can calculate the radius of the area that needs to be covered by the tiles. The radius can
be calculated as:

$$Radius \approx \frac{d}{TileSize}$$

Where TileSize is the width of the tile in meters of the base zoom level.

## Tile Zoom Level Distance Thresholds

- Max rendering distance is 400km.
- Each zoom level has a distance threshold that is half of the previous zoom level.

## Tiles Selection

- Take the camera `Y` position and calculate the distance to the horizon `d`:

$$d \approx \frac{3.57 \cdot \sqrt{Y}}{1000}$$

- Convert the distance to the horizon into a radius in tiles `R` based on the tile size at the base zoom level.
- Iterate the tiles in a square area around the camera position, from `-R` to `+R` in both X and Z directions.

## Tile Subdivision Decision

- For the base zoom level, `MaxDistance` is the distance to the horizon `D`.
- Calculate the distance from the camera to the center of each tile `d`.
- If `d` is greater than `MaxDistance`, skip this tile.
- If `d` is less than `MaxDistance / 2`, subdivide the tile into a higher zoom level and repeat the process for the
  sub-tiles.
- Else use the tile.

## Tile Eviction

- For each tiles in the rendered tiles list
- If it in the desired tiles list, keep it
- If the tiles parent is not loaded and tiles children are not fully loaded, keep it (replacement)
- Else, evict it

### Tiles Selection 2

- Find the tile you're on.
- 