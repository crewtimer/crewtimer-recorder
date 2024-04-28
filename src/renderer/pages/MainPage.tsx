import { Route, Routes } from 'react-router-dom';
import Markdown from '../components/Markdown';
import PrivacyMarkdown from '../doc/PrivacyMarkdown.md';
import HelpMarkdown from '../doc/HelpMarkdown.md';
import HomePage from './HomePage';

const MainPage = () => {
  return (
    <Routes>
      <Route path="/index.html" element={<HomePage />} />
      <Route path="/" element={<HomePage />} />
      <Route path="/privacy" element={<Markdown md={PrivacyMarkdown} />} />
      <Route path="/help" element={<Markdown md={HelpMarkdown} />} />
    </Routes>
  );
};

export default MainPage;
