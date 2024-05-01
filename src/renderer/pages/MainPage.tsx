import { Route, Routes } from 'react-router-dom';
import Markdown from '../components/Markdown';
import PrivacyMarkdown from '../doc/PrivacyMarkdown.md';
import HelpMarkdown from '../doc/HelpMarkdown.md';
import RecorderConfig from '../recorder/RecorderConfig';

const MainPage = () => {
  return (
    <Routes>
      <Route path="/index.html" element={<RecorderConfig />} />
      <Route path="/" element={<RecorderConfig />} />
      <Route path="/home" element={<RecorderConfig />} />
      <Route path="/privacy" element={<Markdown md={PrivacyMarkdown} />} />
      <Route path="/help" element={<Markdown md={HelpMarkdown} />} />
    </Routes>
  );
};

export default MainPage;
