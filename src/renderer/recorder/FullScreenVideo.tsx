import { FullSizeWindow } from '../components/FullSizeWindow';
import PreviewCanvas from '../components/PreviewCanvas';
import ViscaControlPanel from '../visca/ViscaControlPanel';

export const FullScreenVideo = () => {
  return (
    <div
      style={{
        padding: '0px 10px',
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
      }}
    >
      {/* <RecordingError /> */}
      <ViscaControlPanel />
      <FullSizeWindow component={PreviewCanvas} />
    </div>
  );
};
