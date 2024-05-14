import { Route, Routes } from 'react-router-dom';
import Markdown from '../components/Markdown';
import PrivacyMarkdown from '../doc/PrivacyMarkdown.md';
import HelpMarkdown from '../doc/HelpMarkdown.md';
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
      <Route path="/privacy" element={<Markdown md={PrivacyMarkdown} />} />
      <Route path="/help" element={<Markdown md={HelpMarkdown} />} />
    </Routes>
  );
};

export default MainPage;
