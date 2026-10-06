import { FullSizeWindow } from '../components/FullSizeWindow';
import PreviewCanvas from '../components/PreviewCanvas';
import ViscaControlPanel from '../visca/ViscaControlPanel';
import { useRecordingProps } from './RecorderData';
import { useVideoScaling, ZoomMode } from '../util/VideoSettings';

export const FullScreenVideo = () => {
  const [videoScaling] = useVideoScaling();
  const [recordingProps] = useRecordingProps();
  const sourceIsPortrait = videoScaling.srcHeight > videoScaling.srcWidth;
  const cropIsPortrait =
    recordingProps.cropArea.height * videoScaling.srcHeight >
    recordingProps.cropArea.width * videoScaling.srcWidth;
  const isPortrait =
    sourceIsPortrait ||
    (videoScaling.zoomMode === ZoomMode.Maximize && cropIsPortrait);

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: isPortrait ? 'row' : 'column',
        gap: '16px',
        height: '100%',
        minHeight: 0,
      }}
    >
      {/* <RecordingError /> */}
      <div
        style={
          isPortrait
            ? {
                width: '300px',
                maxWidth: '35%',
                flexShrink: 0,
                overflowY: 'auto',
              }
            : undefined
        }
      >
        <ViscaControlPanel vertical={isPortrait} />
      </div>
      <div style={{ flex: 1, minWidth: 0, minHeight: 0 }}>
        <FullSizeWindow component={PreviewCanvas} />
      </div>
    </div>
  );
};
