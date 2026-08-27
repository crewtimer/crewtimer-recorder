# Video Sidecar Metadata

The recorder writes a JSON sidecar next to each video file. The sidecar describes
the recorded image, its location within the original camera feed, and any known
sensor timing behavior.

## Image geometry

The `source` object uses pixels and a top-left origin. Positive X points right and
positive Y points down.

| Field | Meaning |
| --- | --- |
| `source.width` | Width of the recorded video after cropping. |
| `source.height` | Height of the recorded video after cropping. |
| `source.originalWidth` | Width after configured source rotation and before cropping. |
| `source.originalHeight` | Height after configured source rotation and before cropping. |
| `source.crop.x` | Left edge of the recorded video in the original image. |
| `source.crop.y` | Top edge of the recorded video in the original image. |
| `source.crop.width` | Width of the crop. This equals `source.width`. |
| `source.crop.height` | Height of the crop. This equals `source.height`. |

For an uncropped recording, `crop.x` and `crop.y` are zero and the crop dimensions
equal the original dimensions.

Convert a Y coordinate in the recorded video to the original sensor coordinate
before applying a rolling-shutter correction:

```text
originalY = source.crop.y + recordedY
```

## Rolling-shutter timing

The `sensor` object is present in every sidecar. For a landscape raw feed,
`sensor.rollingShutter` contains the assumed sensor model:

| Field | Meaning |
| --- | --- |
| `sensor.nativeWidth` | Width delivered by the physical sensor before source rotation. |
| `sensor.nativeHeight` | Height delivered by the physical sensor before source rotation. |
| `sensor.sourceRotationDegrees` | Clockwise rotation applied at ingestion: `-90`, `0`, or `90`. |
| `direction` | Sensor scan direction in the rotated `original-image` coordinate space. |
| `scanTimeMs` | Time to scan the full image along the scan axis; currently 13.8 ms. |
| `frameTimeReference.coordinateSpace` | `original-image`: the reference coordinate is before cropping but after source rotation. |
| `frameTimeReference.axis` | `x` for a horizontal scan or `y` for a vertical scan. |
| `frameTimeReference.position` | Coordinate represented by the frame timestamp; half the original dimension on the reference axis. |

For raw feeds that are not landscape, `sensor.rollingShutter` is `null`; the
review app must not apply the landscape assumption.

Source rotation also rotates the scan direction into presentation coordinates:

| Source rotation | Scan direction | Reference axis |
| --- | --- | --- |
| 0° | `top-to-bottom` | `y` |
| 90° clockwise | `right-to-left` | `x` |
| 90° counterclockwise | `left-to-right` | `x` |

### Correction formula

Given a Y coordinate in the recorded video, calculate its time relative to the
frame timestamp as follows:

```text
axis = sensor.rollingShutter.frameTimeReference.axis
originalCoordinate = source.crop[axis] + recordedCoordinate
reference = sensor.rollingShutter.frameTimeReference.position
scanDimension = axis == "x" ? source.originalWidth : source.originalHeight

directionSign = direction is "left-to-right" or "top-to-bottom" ? 1 : -1
offsetMs = (originalCoordinate - reference)
           * directionSign
           * sensor.rollingShutter.scanTimeMs / scanDimension

pixelTime = frameTime + offsetMs
```

A positive offset means the row was captured later than the frame timestamp. A
negative offset means it was captured earlier. Increasing coordinates are later
for `left-to-right` and `top-to-bottom`; decreasing coordinates are later for
`right-to-left` and `bottom-to-top`.

For subpixel positions, use the floating-point Y coordinate directly. If the
application represents a row by its upper edge, add `0.5` to use the row center
consistently.

### Example

For a 3840 x 2160 original feed cropped at Y=400, a point at recorded Y=500 maps
to original Y=900. The reference is Y=1080, so:

```text
offsetMs = (900 - 1080) * 13.8 / 2160 = -1.15 ms
```

The point was captured 1.15 ms earlier than the timestamp assigned to the frame.

## Example sidecar fields

```json
{
  "source": {
    "width": 1920,
    "height": 1080,
    "originalWidth": 3840,
    "originalHeight": 2160,
    "crop": {
      "x": 960,
      "y": 400,
      "width": 1920,
      "height": 1080
    }
  },
  "sensor": {
    "nativeWidth": 3840,
    "nativeHeight": 2160,
    "sourceRotationDegrees": 0,
    "rollingShutter": {
      "direction": "top-to-bottom",
      "scanTimeMs": 13.8,
      "frameTimeReference": {
        "coordinateSpace": "original-image",
        "axis": "y",
        "position": 1080.0
      }
    }
  }
}
```
