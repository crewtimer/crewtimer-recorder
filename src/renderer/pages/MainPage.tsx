import { Route, Routes } from 'react-router-dom';
import Markdown from '../components/Markdown';
import PrivacyMarkdown from '../doc/PrivacyMarkdown.md';
import CrewTimerVideoRecorder from '../doc/CrewTimerVideoRecorderHelp.md';
import RecorderConfig from '../recorder/RecorderConfig';
import RecordingLogTable from './RecordingLogTable';
import { FullSizeWindow } from '../components/FullSizeWindow';
import RGBAImageCanvas from '../components/RGBAImageCanvas';

const MainPage = () => {
  return (
    <Routes>
      <Route path="/index.html" element={<RecorderConfig />} />
      <Route path="/" element={<RecorderConfig />} />
      <Route path="/home" element={<RecorderConfig />} />
      <Route path="/log" element={<RecordingLogTable />} />
      <Route
        path="/video"
        element={<FullSizeWindow component={RGBAImageCanvas} />}
      />
      <Route
        path="/privacy"
        element={<Markdown md={PrivacyMarkdown} newWindowLinks />}
      />
      <Route
        path="/help"
        element={<Markdown md={CrewTimerVideoRecorder} newWindowLinks />}
      />
      <Route path="*" element={<RecorderConfig />} />
    </Routes>
  );
};

export default MainPage;
