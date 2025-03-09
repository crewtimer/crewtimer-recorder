import { FullSizeWindow } from '../components/FullSizeWindow';
import RGBAImageCanvas from '../components/RGBAImageCanvas';
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
      <FullSizeWindow component={RGBAImageCanvas} />
    </div>
  );
};
